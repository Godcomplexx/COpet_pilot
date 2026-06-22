#!/usr/bin/env python3
import queue
import struct
import sys
import threading
import time
import warnings
from pathlib import Path

warnings.filterwarnings("ignore", module="requests")

import numpy as np
import pyttsx3
import requests
import serial
from faster_whisper import WhisperModel

try:
    import sounddevice as sd
except Exception:
    sd = None

COM_PORT = sys.argv[1] if len(sys.argv) > 1 else "COM3"
BAUD_RATE = 921600
SAMPLE_RATE = 16000
AUDIO_TARGET_RMS = 0.08
AUDIO_MAX_GAIN = 30.0
AUDIO_PEAK_LIMIT = 0.95
AUDIO_CLIP_LEVEL = 0.995

# Фильтры речи: убрать рокот/наводки (<150 Гц) и внеполосный шум (>7 кГц),
# затем предыскажение (pre-emphasis) для разборчивости согласных.
AUDIO_BANDPASS_LOW = 150.0
AUDIO_BANDPASS_HIGH = 7000.0
AUDIO_PREEMPHASIS = 0.95
from scipy.signal import butter, sosfilt
_BANDPASS_SOS = butter(
    4,
    [AUDIO_BANDPASS_LOW, min(AUDIO_BANDPASS_HIGH, SAMPLE_RATE / 2 - 200.0)],
    btype="band",
    fs=SAMPLE_RATE,
    output="sos",
)


def speech_filter(x: "np.ndarray") -> "np.ndarray":
    if len(x) < 16:
        return x.astype(np.float32)
    y = sosfilt(_BANDPASS_SOS, x).astype(np.float32)
    # pre-emphasis: y[n] -= k*y[n-1]
    y = np.concatenate(([y[0]], y[1:] - AUDIO_PREEMPHASIS * y[:-1])).astype(np.float32)
    return y

WHISPER_SIZE = "base"
WHISPER_LANG = "ru"

OLLAMA_URL = "http://localhost:11434/api/chat"
OLLAMA_MODEL = "gemma4:e2b"
SYSTEM_PROMPT = (
    "Ты голосовой ассистент. Отвечай кратко и по делу, 1-3 предложения. "
    "Без markdown, потому что ответ будет озвучен."
)

TTS_VOICE_HINT = "Irina"
TTS_RATE = 175

MAGIC0, MAGIC1 = 0xA5, 0x5A
TYPE_AUDIO, TYPE_EVENT = 0x01, 0x02
EVENT_PAYLOAD_LENGTHS = {
    9: 1,
    11: 1,
    12: 2,
    13: 2,
}

utterance_queue: "queue.Queue[np.ndarray]" = queue.Queue()


def resolve_whisper_model_path(model_name: str) -> str:
    repo_dir = Path.home() / ".cache" / "huggingface" / "hub" / f"models--Systran--faster-whisper-{model_name}"
    snapshots_dir = repo_dir / "snapshots"
    if snapshots_dir.is_dir():
        snapshots = sorted(
            (path for path in snapshots_dir.iterdir() if path.is_dir()),
            key=lambda path: path.stat().st_mtime,
            reverse=True,
        )
        for snapshot in snapshots:
            required = ("config.json", "model.bin", "tokenizer.json", "vocabulary.txt")
            if all((snapshot / name).is_file() for name in required):
                return str(snapshot)
    return model_name


def open_serial(port: str, baud: int, timeout: float) -> serial.Serial:
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = timeout
    ser.dtr = False
    ser.rts = False
    ser.open()
    ser.dtr = False
    ser.rts = False

    # Сброс ESP в ПРИЛОЖЕНИЕ (нужно для C6 по нативному USB-JTAG; для S3/CH340 безвредно).
    # На Windows смену RTS надо "протолкнуть" сменой DTR, иначе управляющий запрос не уходит.
    def _set_rts(state: bool) -> None:
        ser.setRTS(state)
        ser.setDTR(ser.dtr)

    _set_rts(True)
    time.sleep(0.2)
    _set_rts(False)
    time.sleep(0.3)
    ser.reset_input_buffer()
    return ser


def log(message: str) -> None:
    print(message, flush=True)


def condition_audio(pcm: np.ndarray) -> tuple[np.ndarray, float, float]:
    if len(pcm) == 0:
        return pcm.astype(np.float32), 1.0, 0.0

    original = pcm.astype(np.float32)
    clipped_pct = float(np.mean(np.abs(original) >= AUDIO_CLIP_LEVEL) * 100.0)

    conditioned = original.copy()
    conditioned -= float(np.mean(conditioned))
    conditioned = speech_filter(conditioned)   # полосовой + предыскажение

    rms = float(np.sqrt(np.mean(conditioned ** 2)))
    peak = float(np.max(np.abs(conditioned)))
    if rms <= 1e-6 or peak <= 1e-6:
        return conditioned, 1.0, clipped_pct

    gain = min(AUDIO_TARGET_RMS / rms, AUDIO_PEAK_LIMIT / peak, AUDIO_MAX_GAIN)
    conditioned = np.clip(conditioned * gain, -AUDIO_PEAK_LIMIT, AUDIO_PEAK_LIMIT)
    return conditioned.astype(np.float32), gain, clipped_pct


def submit_audio(label: str, pcm: np.ndarray) -> None:
    seconds = len(pcm) / SAMPLE_RATE
    if len(pcm) < SAMPLE_RATE // 2:
        log(f"[{label}] Слишком коротко: {seconds:.1f} сек")
        return

    # Диагностика: громкость записи + сохранение в WAV для прослушивания.
    pcm, gain, clipped_pct = condition_audio(pcm)
    rms = float(np.sqrt(np.mean(pcm.astype(np.float32) ** 2))) if len(pcm) else 0.0
    peak = float(np.max(np.abs(pcm))) if len(pcm) else 0.0
    log(f"[{label}] audio conditioning: gain={gain:.1f} clipped={clipped_pct:.1f}%")
    if clipped_pct > 1.0:
        log(f"[{label}] WARNING: input audio is clipped before processing; move the mic farther away or lower firmware gain.")
    log(f"[{label}] громкость RMS={rms:.4f} пик={peak:.4f} (норма речи RMS~0.02-0.2)")
    try:
        import wave
        pcm16 = np.clip(pcm * 32768.0, -32768, 32767).astype(np.int16)
        with wave.open("last_record.wav", "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(SAMPLE_RATE)
            w.writeframes(pcm16.tobytes())
        log(f"[{label}] Запись сохранена: last_record.wav")
    except Exception as e:
        log(f"[{label}] WAV не сохранён: {e}")

    utterance_queue.put(pcm.astype(np.float32))
    log(f"[{label}] Запись отправлена на распознавание: {seconds:.1f} сек")


def serial_reader() -> None:
    try:
        ser = open_serial(COM_PORT, BAUD_RATE, timeout=0.5)
        log(f"[serial] Подключено к {COM_PORT} @ {BAUD_RATE}")
    except Exception as e:
        log(f"[serial] Не удалось открыть {COM_PORT}: {e}")
        return

    log("[esp] D13: нажми/держи/говори/отпусти.")
    log("[esp] Если D13 не работает, используй Enter: он пишет микрофон ПК.")

    buf = bytearray()
    current_pcm = bytearray()
    recording = False
    last_rx = time.time()
    warned_silence = False

    while True:
        try:
            chunk = ser.read(4096)
        except Exception as e:
            log(f"[serial] Разрыв соединения: {e}")
            return

        if chunk:
            last_rx = time.time()
            warned_silence = False
            buf.extend(chunk)
        elif not warned_silence and time.time() - last_rx > 5:
            warned_silence = True
            log("[serial] От ESP 5 сек нет аудиоданных. Проверь, что прошивка свежая и ESP перезагружена.")

        while len(buf) >= 4:
            if buf[0] != MAGIC0 or buf[1] != MAGIC1:
                buf.pop(0)
                continue

            frame_type = buf[2]

            if frame_type == TYPE_EVENT:
                code = buf[3]
                event_len = 4 + EVENT_PAYLOAD_LENGTHS.get(code, 0)
                if len(buf) < event_len:
                    break
                del buf[:event_len]
                if code == 1:
                    recording = True
                    current_pcm = bytearray()
                    log("[esp] Запись началась. Говори...")
                elif code == 0:
                    recording = False
                    pcm_i16 = np.frombuffer(bytes(current_pcm), dtype=np.int16)
                    submit_audio("esp", pcm_i16.astype(np.float32) / 32768.0)

            elif frame_type == TYPE_AUDIO:
                if len(buf) < 5:
                    break
                length = struct.unpack_from("<H", buf, 3)[0]
                if len(buf) < 5 + length:
                    break
                payload = bytes(buf[5 : 5 + length])
                del buf[: 5 + length]
                if recording:
                    current_pcm.extend(payload)

            else:
                buf.pop(0)


def pc_mic_keyboard_reader() -> None:
    if sd is None:
        log("[pc mic] sounddevice не установлен, режим Enter недоступен.")
        return

    active = False
    chunks: list[np.ndarray] = []
    stream = None

    def callback(indata, frames, callback_time, status):
        if status:
            log(f"[pc mic] status: {status}")
        chunks.append(indata[:, 0].copy())

    log("[pc mic] Enter -> начать запись с микрофона ПК. Enter ещё раз -> закончить.")

    while True:
        try:
            input()
        except EOFError:
            return

        active = not active
        if active:
            chunks = []
            try:
                stream = sd.InputStream(
                    samplerate=SAMPLE_RATE,
                    channels=1,
                    dtype="float32",
                    callback=callback,
                )
                stream.start()
            except Exception as e:
                active = False
                stream = None
                log(f"[pc mic] Не удалось открыть микрофон ПК: {e}")
                continue
            log("[pc mic] Запись началась. Говори...")
        else:
            if stream is not None:
                stream.stop()
                stream.close()
                stream = None
            pcm = np.concatenate(chunks).astype(np.float32) if chunks else np.zeros(0, dtype=np.float32)
            submit_audio("pc mic", pcm)


class Brain:
    def __init__(self) -> None:
        whisper_model = resolve_whisper_model_path(WHISPER_SIZE)
        if whisper_model != WHISPER_SIZE:
            log(f"[whisper] Загружаю локальную модель: {whisper_model}")
        else:
            log(f"[whisper] Загружаю модель '{WHISPER_SIZE}'...")
        self.asr = WhisperModel(whisper_model, device="cpu", compute_type="int8")

        log("[tts] Инициализация голоса...")
        self.tts = pyttsx3.init()
        self.tts.setProperty("rate", TTS_RATE)
        for voice in self.tts.getProperty("voices"):
            if TTS_VOICE_HINT.lower() in voice.name.lower():
                self.tts.setProperty("voice", voice.id)
                log(f"[tts] Голос: {voice.name}")
                break

        self.history = [{"role": "system", "content": SYSTEM_PROMPT}]
        log("[brain] Готово.")

    def transcribe(self, pcm: np.ndarray) -> str:
        log("[asr] Распознаю речь...")
        segments, info = self.asr.transcribe(
            pcm, language=WHISPER_LANG, beam_size=1, vad_filter=True
        )
        text = "".join(segment.text for segment in segments).strip()
        log(f"[asr] Текст: {text!r}")
        return text

    def think(self, text: str) -> str:
        self.history.append({"role": "user", "content": text})
        log("[ollama] Думаю...")
        try:
            response = requests.post(
                OLLAMA_URL,
                json={
                    "model": OLLAMA_MODEL,
                    "messages": self.history,
                    "stream": False,
                },
                timeout=120,
            )
            response.raise_for_status()
            reply = response.json()["message"]["content"].strip()
        except Exception as e:
            log(f"[ollama] Ошибка: {e}")
            return "Извини, не получилось обработать запрос."

        self.history.append({"role": "assistant", "content": reply})
        if len(self.history) > 21:
            self.history = [self.history[0]] + self.history[-20:]
        return reply

    def speak(self, text: str) -> None:
        log(f"[ответ] {text}")
        self.tts.say(text)
        self.tts.runAndWait()


def main() -> None:
    log("=" * 60)
    log("Локальный голосовой ассистент ESP32 + Whisper + Gemma")
    log(f"Порт ESP: {COM_PORT} | ASR: {WHISPER_SIZE} | LLM: {OLLAMA_MODEL}")
    log("=" * 60)

    brain = Brain()
    threading.Thread(target=serial_reader, daemon=True).start()
    threading.Thread(target=pc_mic_keyboard_reader, daemon=True).start()

    while True:
        pcm = utterance_queue.get()
        text = brain.transcribe(pcm)
        if not text:
            log("[asr] Пусто, пропускаю.")
            continue
        reply = brain.think(text)
        brain.speak(reply)
        log("[ready] Можно говорить снова: D13 или Enter.")


if __name__ == "__main__":
    main()
