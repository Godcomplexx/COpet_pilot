package com.example.espvoiceassistant

import android.Manifest
import android.app.Application
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.OpenableColumns
import android.speech.RecognitionListener
import android.speech.RecognizerIntent
import android.speech.SpeechRecognizer
import android.speech.tts.TextToSpeech
import android.util.Log
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.Alignment
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.vosk.Model
import org.vosk.Recognizer
import com.google.ai.edge.litertlm.Backend
import com.google.ai.edge.litertlm.Content
import com.google.ai.edge.litertlm.Conversation
import com.google.ai.edge.litertlm.Engine
import com.google.ai.edge.litertlm.EngineConfig
import com.google.mediapipe.tasks.genai.llminference.LlmInference
import org.json.JSONArray
import org.json.JSONObject
import java.io.FileOutputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.HttpURLConnection
import java.net.InetAddress
import java.net.Socket
import java.net.URL
import java.net.URLEncoder
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

private const val MAGIC0 = 0xA5.toByte()
private const val MAGIC1 = 0x5A.toByte()
private const val TYPE_AUDIO = 0x01
private const val TYPE_EVENT = 0x02
private const val SAMPLE_RATE = 16_000
private const val APP_LOG_TAG = "ESPVoice"

// Локальная LLM через Ollama на ПК (тот же, что в assistant.py).
private const val OLLAMA_MODEL = "gemma4:e2b"
private const val OLLAMA_SYSTEM_PROMPT =
    "Ты голосовой ассистент. Отвечай кратко и по делу, 1-3 предложения. " +
        "Без markdown, потому что ответ будет озвучен."

class MainActivity : ComponentActivity() {
    private val requestAudioPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) {}

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO)
            != PackageManager.PERMISSION_GRANTED
        ) {
            requestAudioPermission.launch(Manifest.permission.RECORD_AUDIO)
        }

        setContent {
            MaterialTheme {
                Surface(modifier = Modifier.fillMaxSize()) {
                    AssistantScreen()
                }
            }
        }
    }
}

data class AssistantUiState(
    val host: String = "192.168.0.92",
    val port: String = "3333",
    val ollamaHost: String = "",   // IP ПК с Ollama (пусто = LLM выключена)
    val localModelPath: String = "",
    val localModelReady: Boolean = false,
    val localModelLoading: Boolean = false,
    val connected: Boolean = false,
    val discovering: Boolean = false,
    val recording: Boolean = false,
    val processing: Boolean = false,
    val phoneListening: Boolean = false,
    val espAsrReady: Boolean = false,
    val recognizedText: String = "",
    val answer: String = "",
    val log: List<String> = listOf("Готово. Введите IP ESP и нажмите Connect.")
)

class AssistantViewModel(app: Application) : AndroidViewModel(app) {
    private val _state = MutableStateFlow(AssistantUiState())
    val state: StateFlow<AssistantUiState> = _state.asStateFlow()

    private val espClient = EspTcpClient()
    private val discovery = EspDiscovery()
    private val espAsr = EspVoskRecognizer(app.applicationContext, ::log)
    private val duckDuckGo = DuckDuckGoClient()
    private val localLlm = LocalLlmClient()
    private val ollama = OllamaClient()
    private val tts = TtsSpeaker(app.applicationContext, ::log)
    private val speech = PhoneSpeechRecognizer(
        app.applicationContext,
        onLog = ::log,
        onPartial = { text -> _state.update { it.copy(recognizedText = text) } },
        onFinal = { text -> onSpeechFinal(text) },
        onListeningChanged = { active -> _state.update { it.copy(phoneListening = active) } }
    )

    private var connectJob: Job? = null

    init {
        viewModelScope.launch {
            val ready = espAsr.prepare()
            _state.update { it.copy(espAsrReady = ready) }
        }
    }

    fun setHost(value: String) {
        _state.update { it.copy(host = value.trim()) }
    }

    fun setPort(value: String) {
        _state.update { it.copy(port = value.filter(Char::isDigit).take(5)) }
    }

    fun setOllamaHost(value: String) {
        _state.update { it.copy(ollamaHost = value.trim()) }
    }

    fun importLocalModel(uri: Uri) {
        viewModelScope.launch {
            _state.update { it.copy(localModelLoading = true, localModelReady = false) }
            val modelPath = copyLocalModel(uri)
            if (modelPath == null) {
                _state.update { it.copy(localModelLoading = false) }
                return@launch
            }

            val ready = localLlm.load(getApplication<Application>().applicationContext, modelPath, ::log)
            _state.update {
                it.copy(
                    localModelPath = modelPath,
                    localModelReady = ready,
                    localModelLoading = false
                )
            }
        }
    }

    fun discover() {
        if (_state.value.discovering) return
        viewModelScope.launch {
            _state.update { it.copy(discovering = true) }
            log("Ищу ESP в сети (UDP broadcast)...")
            val found = discovery.discover(::log)
            if (found != null) {
                _state.update { it.copy(host = found.first, port = found.second.toString()) }
                log("Найдено: ${found.first}:${found.second}")
            }
            _state.update { it.copy(discovering = false) }
        }
    }

    fun connectOrDisconnect() {
        if (_state.value.connected || connectJob != null) {
            disconnect()
            return
        }

        val host = _state.value.host.ifBlank { "192.168.4.1" }
        val port = _state.value.port.toIntOrNull() ?: 3333

        connectJob = viewModelScope.launch {
            _state.update { it.copy(connected = true) }
            log("Подключаюсь к $host:$port")
            try {
                espClient.run(
                    host = host,
                    port = port,
                    onLog = ::log,
                    onEvent = ::handleEspEvent
                )
            } catch (e: Exception) {
                log("ESP connection error: ${e.message}")
            } finally {
                _state.update { it.copy(connected = false, recording = false, processing = false) }
                connectJob = null
            }
        }
    }

    fun disconnect() {
        speech.stop()
        espClient.close()
        connectJob?.cancel()
        connectJob = null
        _state.update { it.copy(connected = false, recording = false, processing = false) }
        log("Отключено")
    }

    fun togglePhoneMic() {
        if (_state.value.phoneListening) {
            speech.stop()
        } else {
            _state.update { it.copy(recognizedText = "", answer = "", processing = false) }
            speech.start()
        }
    }

    private fun handleEspEvent(event: EspEvent) {
        viewModelScope.launch {
            when (event) {
                EspEvent.Started -> {
                    _state.update { it.copy(recording = true, processing = false, recognizedText = "", answer = "") }
                    log("ESP: запись началась. Говорите.")
                }
                is EspEvent.Stopped -> {
                    _state.update { it.copy(recording = false, processing = true) }
                    val wav = saveWav(event.pcm16)
                    log("ESP: запись остановлена, WAV: ${wav.name}, ${event.pcm16.size} bytes")
                    log(audioStats("ESP raw audio", event.pcm16))
                    recognizeEspRecording(event.pcm16)
                }
            }
        }
    }

    private fun recognizeEspRecording(pcm16: ByteArray) {
        viewModelScope.launch {
            val text = espAsr.transcribe(pcm16)
            _state.update { it.copy(recognizedText = text) }
            if (text.isBlank()) {
                log("ESP ASR: пустой текст")
                _state.update { it.copy(processing = false) }
                return@launch
            }

            try {
                val reply = answerQuestion(text)
                _state.update { it.copy(answer = reply) }
                tts.speak(reply)
            } finally {
                _state.update { it.copy(processing = false) }
            }
        }
    }

    private fun onSpeechFinal(text: String) {
        _state.update { it.copy(recognizedText = text, processing = text.isNotBlank()) }
        if (text.isBlank()) {
            log("ASR: пустой текст")
            return
        }

        viewModelScope.launch {
            try {
                val reply = answerQuestion(text)
                _state.update { it.copy(answer = reply) }
                tts.speak(reply)
            } finally {
                _state.update { it.copy(processing = false) }
            }
        }
    }

    private suspend fun answerQuestion(text: String): String {
        val lower = text.lowercase(Locale("ru"))
        if ("который час" in lower || "сколько времени" in lower) {
            return SimpleDateFormat("HH:mm", Locale("ru")).format(Date())
        }
        if ("какая дата" in lower || "сегодня" in lower) {
            return SimpleDateFormat("d MMMM yyyy", Locale("ru")).format(Date())
        }

        // Основной мозг без ПК: локальные веса Gemma/Gemma 3n/Gemma 4 на телефоне.
        if (_state.value.localModelReady) {
            log("Local LLM: думаю на телефоне...")
            val llm = localLlm.chat(text, ::log)
            if (!llm.isNullOrBlank()) {
                return llm
            }
            log("Local LLM не ответила — проверь, что файл модели подходит MediaPipe/LiteRT.")
        }

        // Запасной мозг: Ollama (Gemma) на ПК, если указан его IP.
        val ollamaHost = _state.value.ollamaHost.trim()
        if (ollamaHost.isNotBlank()) {
            log("Ollama: думаю ($OLLAMA_MODEL)...")
            val llm = ollama.chat(ollamaHost, text, ::log)
            if (!llm.isNullOrBlank()) {
                return llm
            }
            log("Ollama не ответил — проверь IP ПК и что Ollama слушает сеть (OLLAMA_HOST=0.0.0.0).")
        }

        val ddg = duckDuckGo.answer(text)
        if (!ddg.isNullOrBlank()) {
            return ddg
        }

        return if (!_state.value.localModelReady && ollamaHost.isBlank()) {
            "Локальная модель не загружена. Нажми «Pick model» и выбери файл .task или .litertlm на телефоне."
        } else {
            "Не нашла точный ответ."
        }
    }

    private suspend fun copyLocalModel(uri: Uri): String? = withContext(Dispatchers.IO) {
        try {
            val context = getApplication<Application>().applicationContext
            val name = context.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
                val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                if (cursor.moveToFirst() && index >= 0) cursor.getString(index) else null
            }.orEmpty()
            val extension = when {
                name.endsWith(".litertlm", ignoreCase = true) -> ".litertlm"
                else -> ".task"
            }
            val outFile = File(context.filesDir, "local_llm_model$extension")
            File(context.filesDir, "local_llm_model.task").takeIf { it != outFile }?.delete()
            File(context.filesDir, "local_llm_model.litertlm").takeIf { it != outFile }?.delete()
            context.contentResolver.openInputStream(uri)?.use { input ->
                FileOutputStream(outFile).use { output -> input.copyTo(output) }
            } ?: run {
                log("Local LLM: не удалось открыть файл модели")
                return@withContext null
            }
            log("Local LLM: модель скопирована в ${outFile.name} (${outFile.length() / 1024 / 1024} MB)")
            outFile.absolutePath
        } catch (e: Exception) {
            log("Local LLM: ошибка копирования модели: ${e.message}")
            null
        }
    }

    private suspend fun saveWav(pcm16: ByteArray): File = withContext(Dispatchers.IO) {
        // Сырая запись (до фильтра) — для диагностики/настройки.
        File(getApplication<Application>().filesDir, "last_esp_raw.wav").writeBytes(buildWav(pcm16))
        val file = File(getApplication<Application>().filesDir, "last_esp_record.wav")
        file.writeBytes(buildWav(enhanceForAsr(pcm16)))   // то, что слышит Vosk (после фильтра)
        file
    }

    private fun log(message: String) {
        Log.d(APP_LOG_TAG, message)
        _state.update { state ->
            state.copy(log = (state.log + message).takeLast(300))
        }
    }

    override fun onCleared() {
        disconnect()
        speech.destroy()
        localLlm.close()
        tts.shutdown()
        super.onCleared()
    }
}

sealed interface EspEvent {
    data object Started : EspEvent
    data class Stopped(val pcm16: ByteArray) : EspEvent
}

class EspTcpClient {
    @Volatile
    private var socket: Socket? = null

    suspend fun run(
        host: String,
        port: Int,
        onLog: (String) -> Unit,
        onEvent: (EspEvent) -> Unit
    ) = withContext(Dispatchers.IO) {
        Socket(host, port).use { sock ->
            socket = sock
            sock.soTimeout = 700
            onLog("Wi-Fi connected: $host:$port")

            val input = sock.getInputStream()
            // Индексный буфер с уплотнением: O(n) на поток (ESP льёт ~32 КБ/с непрерывно).
            var buf = ByteArray(16384)
            var len = 0
            val chunk = ByteArray(8192)
            val recording = ByteArrayOutputStream()
            var isRecording = false
            var lastRx = System.currentTimeMillis()

            while (!Thread.currentThread().isInterrupted && !sock.isClosed) {
                val count = try {
                    input.read(chunk)
                } catch (_: java.net.SocketTimeoutException) {
                    0
                }

                if (count < 0) break
                if (count > 0) {
                    lastRx = System.currentTimeMillis()
                    if (len + count > buf.size) {
                        buf = buf.copyOf(maxOf(buf.size * 2, len + count))
                    }
                    System.arraycopy(chunk, 0, buf, len, count)
                    len += count
                } else if (System.currentTimeMillis() - lastRx > 5000) {
                    onLog("ESP: 5 секунд нет данных")
                    lastRx = System.currentTimeMillis()
                }

                var pos = 0
                frames@ while (len - pos >= 4) {
                    if (buf[pos] != MAGIC0 || buf[pos + 1] != MAGIC1) {
                        pos++
                        continue
                    }

                    when (buf[pos + 2].toInt() and 0xFF) {
                        TYPE_EVENT -> {
                            val code = buf[pos + 3].toInt() and 0xFF
                            pos += 4
                            when (code) {
                                1 -> {
                                    isRecording = true
                                    recording.reset()
                                    onEvent(EspEvent.Started)
                                }
                                0 -> {
                                    isRecording = false
                                    onEvent(EspEvent.Stopped(recording.toByteArray()))
                                }
                            }
                        }
                        TYPE_AUDIO -> {
                            if (len - pos < 5) break@frames
                            val frameLen = (buf[pos + 3].toInt() and 0xFF) or
                                ((buf[pos + 4].toInt() and 0xFF) shl 8)
                            if (len - pos < 5 + frameLen) break@frames
                            if (isRecording) {
                                recording.write(buf, pos + 5, frameLen)
                            }
                            pos += 5 + frameLen
                        }
                        else -> pos++
                    }
                }

                if (pos > 0) {
                    System.arraycopy(buf, pos, buf, 0, len - pos)
                    len -= pos
                }
            }
        }
    }

    fun close() {
        try {
            socket?.close()
        } catch (_: IOException) {
        } finally {
            socket = null
        }
    }
}

class EspVoskRecognizer(
    private val context: Context,
    private val onLog: (String) -> Unit
) {
    private var model: Model? = null

    suspend fun prepare(): Boolean = withContext(Dispatchers.IO) {
        if (model != null) return@withContext true

        val modelDir = File(context.filesDir, "vosk-model-ru")
        val marker = File(modelDir, ".ready")
        if (!marker.isFile) {
            val copied = copyAssetModel(modelDir)
            if (!copied) {
                onLog("ESP ASR: нет assets/model-ru. Запусти scripts/download_vosk_ru_model.ps1 и пересобери APK.")
                return@withContext false
            }
            marker.writeText("ok")
        }

        try {
            model = Model(modelDir.absolutePath)
            onLog("ESP ASR: Vosk модель готова")
            true
        } catch (e: IOException) {
            onLog("ESP ASR: не удалось загрузить модель: ${e.message}")
            false
        }
    }

    suspend fun transcribe(pcm16: ByteArray): String = withContext(Dispatchers.IO) {
        val currentModel = model ?: run {
            if (!prepare()) return@withContext ""
            model ?: return@withContext ""
        }

        if (pcm16.size < SAMPLE_RATE) {
            onLog("ESP ASR: запись слишком короткая")
            return@withContext ""
        }

        try {
            val enhanced = enhanceForAsr(pcm16)
            onLog(audioStats("ESP filtered audio", enhanced))
            val filteredText = recognizeWithVosk(currentModel, enhanced)
            if (filteredText.isNotBlank()) {
                onLog("ESP ASR filtered: '$filteredText'")
                filteredText
            } else {
                onLog("ESP ASR filtered пусто, пробую raw без фильтра")
                val rawText = recognizeWithVosk(currentModel, pcm16)
                onLog("ESP ASR raw: '$rawText'")
                rawText
            }
        } catch (e: Exception) {
            onLog("ESP ASR: ошибка распознавания: ${e.message}")
            ""
        }
    }

    private fun recognizeWithVosk(currentModel: Model, pcm16: ByteArray): String {
        Recognizer(currentModel, SAMPLE_RATE.toFloat()).use { recognizer ->
            recognizer.acceptWaveForm(pcm16, pcm16.size)
            val json = JSONObject(recognizer.finalResult)
            return json.optString("text").trim()
        }
    }

    private fun copyAssetModel(targetDir: File): Boolean {
        return try {
            val rootItems = context.assets.list("model-ru")
            if (rootItems.isNullOrEmpty()) return false

            if (targetDir.exists()) targetDir.deleteRecursively()
            targetDir.mkdirs()
            copyAssetDir("model-ru", targetDir)
            true
        } catch (e: Exception) {
            onLog("ESP ASR: ошибка копирования модели: ${e.message}")
            false
        }
    }

    private fun copyAssetDir(assetPath: String, outDir: File) {
        val items = context.assets.list(assetPath).orEmpty()
        if (items.isEmpty()) {
            context.assets.open(assetPath).use { input ->
                FileOutputStream(outDir).use { output ->
                    input.copyTo(output)
                }
            }
            return
        }

        outDir.mkdirs()
        for (item in items) {
            val childAsset = "$assetPath/$item"
            val childOut = File(outDir, item)
            val childItems = context.assets.list(childAsset).orEmpty()
            if (childItems.isEmpty()) {
                context.assets.open(childAsset).use { input ->
                    FileOutputStream(childOut).use { output ->
                        input.copyTo(output)
                    }
                }
            } else {
                copyAssetDir(childAsset, childOut)
            }
        }
    }
}

class PhoneSpeechRecognizer(
    private val context: Context,
    private val onLog: (String) -> Unit,
    private val onPartial: (String) -> Unit,
    private val onFinal: (String) -> Unit,
    private val onListeningChanged: (Boolean) -> Unit
) {
    private var recognizer: SpeechRecognizer? = null

    fun start() {
        if (!SpeechRecognizer.isRecognitionAvailable(context)) {
            onLog("ASR: SpeechRecognizer недоступен")
            return
        }

        if (recognizer == null) {
            recognizer = createRecognizer()
            recognizer?.setRecognitionListener(listener)
        }

        val intent = Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).apply {
            putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
            putExtra(RecognizerIntent.EXTRA_LANGUAGE, "ru-RU")
            putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true)
            putExtra(RecognizerIntent.EXTRA_PREFER_OFFLINE, true)
        }
        onListeningChanged(true)
        recognizer?.startListening(intent)
        onLog("ASR: слушаю микрофон телефона")
    }

    fun stop() {
        try {
            recognizer?.stopListening()
        } catch (_: Exception) {
        }
        onListeningChanged(false)
    }

    fun destroy() {
        recognizer?.destroy()
        recognizer = null
    }

    private fun createRecognizer(): SpeechRecognizer {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
            SpeechRecognizer.isOnDeviceRecognitionAvailable(context)
        ) {
            onLog("ASR: on-device SpeechRecognizer")
            SpeechRecognizer.createOnDeviceSpeechRecognizer(context)
        } else {
            onLog("ASR: системный SpeechRecognizer")
            SpeechRecognizer.createSpeechRecognizer(context)
        }
    }

    private val listener = object : RecognitionListener {
        override fun onReadyForSpeech(params: Bundle?) = Unit
        override fun onBeginningOfSpeech() = Unit
        override fun onRmsChanged(rmsdB: Float) = Unit
        override fun onBufferReceived(buffer: ByteArray?) = Unit
        override fun onEndOfSpeech() {
            onListeningChanged(false)
        }

        override fun onError(error: Int) {
            onListeningChanged(false)
            onLog("ASR error: $error")
        }

        override fun onResults(results: Bundle?) {
            onListeningChanged(false)
            val text = results
                ?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)
                ?.firstOrNull()
                .orEmpty()
            onFinal(text)
        }

        override fun onPartialResults(partialResults: Bundle?) {
            val text = partialResults
                ?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)
                ?.firstOrNull()
                .orEmpty()
            if (text.isNotBlank()) onPartial(text)
        }

        override fun onEvent(eventType: Int, params: Bundle?) = Unit
    }
}

class EspDiscovery {
    // Шлёт UDP-broadcast "ESP32C6_MIC?" на порт 3334; прошивка C6 отвечает
    // строкой "ESP32C6_MIC;ip=...;tcp=3333;mode=...". Возвращает (ip, tcpPort).
    suspend fun discover(onLog: (String) -> Unit): Pair<String, Int>? = withContext(Dispatchers.IO) {
        try {
            DatagramSocket().use { sock ->
                sock.broadcast = true
                sock.soTimeout = 2500
                val req = WIFI_DISCOVERY_REQ.toByteArray()
                sock.send(
                    DatagramPacket(
                        req, req.size,
                        InetAddress.getByName("255.255.255.255"), WIFI_DISCOVERY_PORT
                    )
                )
                val reply = DatagramPacket(ByteArray(256), 256)
                sock.receive(reply)
                val msg = String(reply.data, 0, reply.length)
                onLog("Discover ответ: $msg")
                val ip = Regex("ip=([0-9.]+)").find(msg)?.groupValues?.get(1)
                val tcp = Regex("tcp=([0-9]+)").find(msg)?.groupValues?.get(1)?.toIntOrNull() ?: 3333
                if (ip != null) ip to tcp else null
            }
        } catch (e: Exception) {
            onLog("Discover: устройство не найдено (${e.javaClass.simpleName})")
            null
        }
    }

    companion object {
        private const val WIFI_DISCOVERY_PORT = 3334
        private const val WIFI_DISCOVERY_REQ = "ESP32C6_MIC?"
    }
}

class OllamaClient {
    // Шлёт вопрос в Ollama на ПК (http://<ip>:11434/api/chat) и возвращает ответ Gemma.
    suspend fun chat(host: String, question: String, onLog: (String) -> Unit): String? =
        withContext(Dispatchers.IO) {
            try {
                val url = URL("http://$host:11434/api/chat")
                val conn = (url.openConnection() as HttpURLConnection).apply {
                    connectTimeout = 5000
                    readTimeout = 60000
                    requestMethod = "POST"
                    doOutput = true
                    setRequestProperty("Content-Type", "application/json")
                }
                val payload = JSONObject().apply {
                    put("model", OLLAMA_MODEL)
                    put("stream", false)
                    put("messages", JSONArray().apply {
                        put(JSONObject().put("role", "system").put("content", OLLAMA_SYSTEM_PROMPT))
                        put(JSONObject().put("role", "user").put("content", question))
                    })
                }
                conn.outputStream.use { it.write(payload.toString().toByteArray(Charsets.UTF_8)) }
                val body = conn.inputStream.bufferedReader(Charsets.UTF_8).use { it.readText() }
                JSONObject(body).getJSONObject("message").getString("content").trim()
            } catch (e: Exception) {
                onLog("Ollama: ${e.javaClass.simpleName} ${e.message ?: ""}")
                null
            }
        }
}

class LocalLlmClient {
    private var mediaPipeInference: LlmInference? = null
    private var liteRtEngine: Engine? = null
    private var liteRtConversation: Conversation? = null
    private var loadedPath: String? = null

    suspend fun load(context: Context, modelPath: String, onLog: (String) -> Unit): Boolean =
        withContext(Dispatchers.IO) {
            try {
                if ((mediaPipeInference != null || liteRtEngine != null) && loadedPath == modelPath) {
                    return@withContext true
                }

                close()
                when (detectModelFormat(modelPath)) {
                    LocalModelFormat.LITERT_LM -> {
                        onLog("Local LLM: загружаю LiteRT-LM модель...")
                        val config = EngineConfig(
                            modelPath = modelPath,
                            backend = Backend.CPU(4),
                            maxNumTokens = 2048,
                            cacheDir = context.cacheDir.absolutePath
                        )
                        liteRtEngine = Engine(config).also { engine ->
                            engine.initialize()
                            liteRtConversation = engine.createConversation()
                        }
                        onLog("Local LLM: LiteRT-LM модель загружена")
                    }
                    LocalModelFormat.MEDIAPIPE_TASK -> {
                        onLog("Local LLM: загружаю MediaPipe task модель...")
                        val options = LlmInference.LlmInferenceOptions.builder()
                            .setModelPath(modelPath)
                            .setMaxTokens(1024)
                            .build()
                        mediaPipeInference = LlmInference.createFromOptions(context, options)
                        onLog("Local LLM: MediaPipe модель загружена")
                    }
                    LocalModelFormat.WEB_TASK -> {
                        onLog("Local LLM: это web.task/TFL3 файл. Для телефона скачай .litertlm: gemma-4-E2B-it.litertlm или gemma-4-E2B-it-web.litertlm.")
                        return@withContext false
                    }
                    LocalModelFormat.UNKNOWN -> {
                        onLog("Local LLM: неизвестный формат модели. Нужен .litertlm или MediaPipe .task bundle.")
                        return@withContext false
                    }
                }
                loadedPath = modelPath
                true
            } catch (e: Exception) {
                onLog("Local LLM: ошибка загрузки ${e.javaClass.simpleName}: ${e.message ?: ""}")
                close()
                false
            }
        }

    suspend fun chat(question: String, onLog: (String) -> Unit): String? =
        withContext(Dispatchers.IO) {
            try {
                val prompt = "$OLLAMA_SYSTEM_PROMPT\n\nВопрос: $question\nОтвет:"
                liteRtConversation?.let { conversation ->
                    return@withContext conversation.sendMessage(prompt)
                        .contents
                        .contents
                        .joinToString(separator = "") { content ->
                            when (content) {
                                is Content.Text -> content.text
                                else -> content.toString()
                            }
                        }
                        .trim()
                        .ifBlank { null }
                }
                mediaPipeInference?.generateResponse(prompt)?.trim()?.ifBlank { null }
            } catch (e: Exception) {
                onLog("Local LLM: ошибка генерации ${e.javaClass.simpleName}: ${e.message ?: ""}")
                null
            }
        }

    fun close() {
        try {
            liteRtConversation?.close()
        } catch (_: Exception) {
        }
        try {
            liteRtEngine?.close()
        } catch (_: Exception) {
        }
        try {
            mediaPipeInference?.close()
        } catch (_: Exception) {
        } finally {
            mediaPipeInference = null
            liteRtConversation = null
            liteRtEngine = null
            loadedPath = null
        }
    }

    private fun detectModelFormat(modelPath: String): LocalModelFormat {
        val file = File(modelPath)
        val header = ByteArray(8)
        val read = try {
            file.inputStream().use { it.read(header) }
        } catch (_: Exception) {
            0
        }
        val lowerName = file.name.lowercase(Locale.US)
        val isLiteRtLm = read >= 8 &&
            header[0] == 'L'.code.toByte() &&
            header[1] == 'I'.code.toByte() &&
            header[2] == 'T'.code.toByte() &&
            header[3] == 'E'.code.toByte() &&
            header[4] == 'R'.code.toByte() &&
            header[5] == 'T'.code.toByte() &&
            header[6] == 'L'.code.toByte() &&
            header[7] == 'M'.code.toByte()
        val isZipTask = read >= 2 && header[0] == 'P'.code.toByte() && header[1] == 'K'.code.toByte()
        val isTfliteFlatbuffer = read >= 8 &&
            header[4] == 'T'.code.toByte() &&
            header[5] == 'F'.code.toByte() &&
            header[6] == 'L'.code.toByte() &&
            header[7] == '3'.code.toByte()

        return when {
            isLiteRtLm -> LocalModelFormat.LITERT_LM
            lowerName.endsWith(".task") && isZipTask -> LocalModelFormat.MEDIAPIPE_TASK
            lowerName.endsWith(".task") && isTfliteFlatbuffer -> LocalModelFormat.WEB_TASK
            lowerName.endsWith(".task") -> LocalModelFormat.MEDIAPIPE_TASK
            isZipTask -> LocalModelFormat.MEDIAPIPE_TASK
            else -> LocalModelFormat.UNKNOWN
        }
    }

    private enum class LocalModelFormat {
        LITERT_LM,
        MEDIAPIPE_TASK,
        WEB_TASK,
        UNKNOWN
    }
}

class DuckDuckGoClient {
    suspend fun answer(question: String): String? = withContext(Dispatchers.IO) {
        try {
            val encoded = URLEncoder.encode(question, "UTF-8")
            val url = URL("https://api.duckduckgo.com/?q=$encoded&format=json&no_html=1&skip_disambig=1")
            val conn = (url.openConnection() as HttpURLConnection).apply {
                connectTimeout = 6000
                readTimeout = 8000
                requestMethod = "GET"
            }

            val body = conn.inputStream.bufferedReader().use { it.readText() }
            val json = JSONObject(body)
            val abstractText = json.optString("AbstractText").trim()
            val answer = json.optString("Answer").trim()
            val heading = json.optString("Heading").trim()

            when {
                abstractText.isNotBlank() -> abstractText.take(500)
                answer.isNotBlank() -> answer.take(500)
                heading.isNotBlank() -> "Нашла: $heading"
                else -> null
            }
        } catch (_: Exception) {
            null
        }
    }
}

class TtsSpeaker(context: Context, private val onLog: (String) -> Unit) : TextToSpeech.OnInitListener {
    private var ready = false
    private val tts = TextToSpeech(context, this)

    override fun onInit(status: Int) {
        ready = status == TextToSpeech.SUCCESS
        if (ready) {
            tts.language = Locale("ru")
            tts.setSpeechRate(1.0f)
            onLog("TTS: готов")
        } else {
            onLog("TTS: ошибка инициализации")
        }
    }

    fun speak(text: String) {
        if (!ready || text.isBlank()) return
        tts.speak(text, TextToSpeech.QUEUE_FLUSH, null, "assistant-answer")
    }

    fun shutdown() {
        tts.shutdown()
    }
}

@Composable
fun AssistantScreen(viewModel: AssistantViewModel = androidx.lifecycle.viewmodel.compose.viewModel()) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    val modelPicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument(),
        onResult = { uri -> if (uri != null) viewModel.importLocalModel(uri) }
    )

    DisposableEffect(Unit) {
        onDispose { viewModel.disconnect() }
    }

    AssistantContent(
        state = state,
        onHostChange = viewModel::setHost,
        onPortChange = viewModel::setPort,
        onOllamaHostChange = viewModel::setOllamaHost,
        onPickModelClick = { modelPicker.launch(arrayOf("*/*")) },
        onDiscoverClick = viewModel::discover,
        onConnectClick = viewModel::connectOrDisconnect,
        onMicClick = viewModel::togglePhoneMic
    )
}

@Composable
fun AssistantContent(
    state: AssistantUiState,
    onHostChange: (String) -> Unit,
    onPortChange: (String) -> Unit,
    onOllamaHostChange: (String) -> Unit,
    onPickModelClick: () -> Unit,
    onDiscoverClick: () -> Unit,
    onConnectClick: () -> Unit,
    onMicClick: () -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedTextField(
                value = state.host,
                onValueChange = onHostChange,
                label = { Text("ESP IP") },
                modifier = Modifier.weight(1f),
                singleLine = true
            )
            OutlinedTextField(
                value = state.port,
                onValueChange = onPortChange,
                label = { Text("TCP") },
                modifier = Modifier.width(92.dp),
                singleLine = true
            )
        }

        OutlinedTextField(
            value = state.ollamaHost,
            onValueChange = onOllamaHostChange,
            label = { Text("Ollama PC IP (запасной вариант)") },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true
        )

        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = onDiscoverClick, enabled = !state.discovering) {
                Text(if (state.discovering) "..." else "Discover")
            }
            Button(onClick = onConnectClick) {
                Text(if (state.connected) "Disconnect" else "Connect")
            }
        }

        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = onMicClick) {
                Text(if (state.phoneListening) "Stop mic" else "Phone mic")
            }
            Button(onClick = onPickModelClick, enabled = !state.localModelLoading) {
                Text(if (state.localModelLoading) "Loading..." else "Pick model")
            }
        }

        Text(
            text = if (state.espAsrReady) "ESP ASR: Vosk local ready" else "ESP ASR: model not loaded",
            color = if (state.espAsrReady) Color(0xFF2F6F4E) else Color(0xFF9A3412),
            fontWeight = FontWeight.Bold
        )

        Text(
            text = when {
                state.localModelLoading -> "Local LLM: loading model"
                state.localModelReady -> "Local LLM: ready"
                else -> "Local LLM: pick .task/.litertlm model"
            },
            color = if (state.localModelReady) Color(0xFF2F6F4E) else Color(0xFF9A3412),
            fontWeight = FontWeight.Bold
        )

        StatusBanner(
            recording = state.recording,
            processing = state.processing,
            phoneListening = state.phoneListening
        )

        Text("Recognized text", fontWeight = FontWeight.Bold)
        Card(modifier = Modifier.fillMaxWidth()) {
            Text(
                text = state.recognizedText.ifBlank { "..." },
                modifier = Modifier.padding(12.dp).fillMaxWidth()
            )
        }

        Text("Assistant answer", fontWeight = FontWeight.Bold)
        Card(modifier = Modifier.fillMaxWidth()) {
            Text(
                text = state.answer.ifBlank { "..." },
                modifier = Modifier.padding(12.dp).fillMaxWidth()
            )
        }

        Text("Log", fontWeight = FontWeight.Bold)
        SelectionContainer {
            Text(
                text = state.log.joinToString("\n"),
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .background(Color(0xFFF7F7F7))
                    .padding(8.dp)
                    .verticalScroll(rememberScrollState()),
                fontFamily = FontFamily.Monospace
            )
        }

        Spacer(modifier = Modifier.height(2.dp))
    }
}

@Composable
fun StatusBanner(
    recording: Boolean,
    processing: Boolean,
    phoneListening: Boolean
) {
    val active = recording || processing || phoneListening
    val transition = rememberInfiniteTransition(label = "statusPulse")
    val pulseScale by transition.animateFloat(
        initialValue = 1f,
        targetValue = if (active) 1.65f else 1f,
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = if (processing) 420 else 700),
            repeatMode = RepeatMode.Restart
        ),
        label = "pulseScale"
    )
    val pulseAlpha by transition.animateFloat(
        initialValue = if (active) 0.45f else 0f,
        targetValue = 0f,
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = if (processing) 420 else 700),
            repeatMode = RepeatMode.Restart
        ),
        label = "pulseAlpha"
    )

    val bannerText = when {
        recording -> "СЛУШАЮ"
        processing -> "ОБРАБОТКА"
        phoneListening -> "СЛУШАЮ ТЕЛЕФОН"
        else -> "ОЖИДАНИЕ"
    }
    val detailText = when {
        recording -> "ESP записывает звук"
        processing -> "Распознаю и готовлю ответ"
        phoneListening -> "Микрофон телефона активен"
        else -> "Нажми кнопку на ESP или Phone mic"
    }
    val bannerColor = when {
        recording -> Color(0xFF1F7A4D)
        processing -> Color(0xFFB45309)
        phoneListening -> Color(0xFF315F96)
        else -> Color(0xFFECEFF3)
    }
    val textColor = if (active) Color.White else Color(0xFF374151)

    Card(colors = CardDefaults.cardColors(containerColor = bannerColor)) {
        Row(
            modifier = Modifier
                .padding(16.dp)
                .fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Box(
                modifier = Modifier.size(34.dp),
                contentAlignment = Alignment.Center
            ) {
                Box(
                    modifier = Modifier
                        .size(22.dp)
                        .scale(pulseScale)
                        .alpha(pulseAlpha)
                        .background(textColor, CircleShape)
                )
                Box(
                    modifier = Modifier
                        .size(if (active) 18.dp else 14.dp)
                        .background(textColor, CircleShape)
                )
            }
            Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Text(
                    text = bannerText,
                    color = textColor,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    text = detailText,
                    color = textColor.copy(alpha = 0.88f)
                )
            }
        }
    }
}

// Обработка для ASR: DC-removal + ФВЧ ~120 Гц (убирает рокот/наводку 125 Гц)
// + предыскажение (поднимает согласные) + нормализация с ограничением усиления.
// Помогает вытянуть слабый/мутный сигнал аналогового микрофона.
private fun enhanceForAsr(pcm16: ByteArray): ByteArray {
    val n = pcm16.size / 2
    if (n < 16) return pcm16

    val inBuf = ByteBuffer.wrap(pcm16).order(ByteOrder.LITTLE_ENDIAN)
    val x = FloatArray(n)
    for (i in 0 until n) x[i] = inBuf.short.toFloat()

    // DC-removal
    var mean = 0f
    for (v in x) mean += v
    mean /= n
    for (i in 0 until n) x[i] -= mean

    // Лёгкий ФНЧ ~3.5 кГц: срезает высокочастотный шип, голос (250-3000 Гц) сохраняется.
    // Раньше тут были агрессивный ФВЧ + предыскажение — они "съедали" низкий сигнал
    // аналогового мика и раздували шум. Убрано.
    val lp = 0.55f
    var ly = 0f
    for (i in 0 until n) {
        ly += lp * (x[i] - ly)
        x[i] = ly
    }

    // Нормализация по пику с потолком усиления (чтобы не раздувать шум до бесконечности)
    var peak = 1f
    for (v in x) {
        val av = kotlin.math.abs(v)
        if (av > peak) peak = av
    }
    val gain = minOf(0.6f * 32768f / peak, 30f)

    val out = ByteArray(n * 2)
    val outBuf = ByteBuffer.wrap(out).order(ByteOrder.LITTLE_ENDIAN)
    for (i in 0 until n) {
        var s = x[i] * gain
        if (s > 32767f) s = 32767f else if (s < -32768f) s = -32768f
        outBuf.putShort(s.toInt().toShort())
    }
    return out
}

private fun audioStats(label: String, pcm16: ByteArray): String {
    val sampleCount = pcm16.size / 2
    if (sampleCount == 0) {
        return "$label: empty, bytes=${pcm16.size}"
    }

    val inBuf = ByteBuffer.wrap(pcm16, 0, sampleCount * 2).order(ByteOrder.LITTLE_ENDIAN)
    var peak = 0
    var sumSquares = 0.0
    repeat(sampleCount) {
        val sample = inBuf.short.toInt()
        val abs = if (sample < 0) -sample else sample
        if (abs > peak) peak = abs
        sumSquares += sample.toDouble() * sample.toDouble()
    }

    val rms = kotlin.math.sqrt(sumSquares / sampleCount)
    val durationMs = sampleCount * 1000L / SAMPLE_RATE
    return String.format(
        Locale.US,
        "%s: duration=%d ms, samples=%d, bytes=%d, peak=%d, rms=%.1f",
        label,
        durationMs,
        sampleCount,
        pcm16.size,
        peak,
        rms
    )
}

private fun buildWav(pcm16: ByteArray): ByteArray {
    val dataSize = pcm16.size
    val totalSize = 36 + dataSize
    val byteRate = SAMPLE_RATE * 2
    val header = ByteBuffer.allocate(44).order(ByteOrder.LITTLE_ENDIAN)
    header.put("RIFF".toByteArray())
    header.putInt(totalSize)
    header.put("WAVE".toByteArray())
    header.put("fmt ".toByteArray())
    header.putInt(16)
    header.putShort(1.toShort())
    header.putShort(1.toShort())
    header.putInt(SAMPLE_RATE)
    header.putInt(byteRate)
    header.putShort(2.toShort())
    header.putShort(16.toShort())
    header.put("data".toByteArray())
    header.putInt(dataSize)
    return header.array() + pcm16
}
