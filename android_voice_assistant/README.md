# ESP Voice for Android

[Русский](#русский) · [English](#english)

## Русский

### Назначение

ESP Voice — Android-клиент для голосового ассистента на базе Seeed Studio XIAO ESP32-C6. Приложение принимает по Wi-Fi звук с микрофона платы, распознаёт русскую речь локально через Vosk, формирует ответ и озвучивает его средствами Android TextToSpeech.

Приложение также поддерживает микрофон телефона, локальные LLM на устройстве и резервное подключение к Ollama на компьютере. ПК не требуется, если на телефоне загружена совместимая локальная модель либо достаточно встроенных команд и DuckDuckGo fallback.

### Основные характеристики

| Параметр | Значение |
|---|---|
| Application ID | `com.example.espvoiceassistant` |
| Версия | `0.1` (`versionCode 1`) |
| Минимальная версия Android | Android 8.0 / API 26 |
| Target / Compile SDK | API 35 |
| Язык и UI | Kotlin, Jetpack Compose, Material 3 |
| Java toolchain | Java 17 |
| ESP transport | TCP, порт `3333` |
| ESP discovery | UDP broadcast, порт `3334` |
| Формат аудио | mono PCM, 16 кГц, signed 16-bit LE |
| ESP ASR | Vosk Android `0.3.75` |
| Локальная LLM | LiteRT-LM или MediaPipe LLM Inference |

### Как обрабатывается запрос

```text
ESP microphone
  → TCP PCM stream
  → фильтрация и нормализация
  → локальный Vosk ASR
  → выбор источника ответа
  → Android TextToSpeech

Phone microphone
  → Android SpeechRecognizer
  → выбор источника ответа
  → Android TextToSpeech
```

Источники ответа проверяются в следующем порядке:

1. Встроенные команды времени и даты.
2. Локальная модель `.litertlm` или совместимый MediaPipe `.task` bundle.
3. Ollama на ПК, если указан IP компьютера.
4. DuckDuckGo Instant Answer при наличии интернета.
5. Информационное сообщение, если подходящий ответ не найден.

### Требования

- Android Studio с поддержкой Android Gradle Plugin 8.13.2.
- JDK 17.
- Android SDK 35.
- Телефон или планшет с Android 8.0 или новее.
- ESP32-C6 с прошивкой из `../esp_serial_mic_c6`.
- Телефон и ESP в одной Wi-Fi сети либо телефон, подключённый к точке доступа ESP.
- Разрешение на запись звука для режима `Phone mic`.
- Достаточно свободной памяти для выбранной локальной LLM.

### Сборка и запуск

В репозитории нет полного Gradle Wrapper, поэтому рекомендуемый способ сборки — Android Studio.

1. Откройте каталог `android_voice_assistant` как отдельный проект.
2. Выберите JDK 17 для Gradle.
3. Установите Android SDK 35, если Android Studio предложит это сделать.
4. Дождитесь завершения Gradle Sync и загрузки зависимостей.
5. Подключите Android-устройство с включённой USB-отладкой.
6. Запустите конфигурацию `app`.
7. При первом запуске разрешите доступ к микрофону.

### Модель Vosk

Русская модель Vosk хранится в:

```text
app/src/main/assets/model-ru/
```

Она копируется во внутреннее хранилище приложения при подготовке ASR. Готовность отображается строкой `ESP ASR: Vosk local ready`.

Если модель была удалена или повреждена, восстановите её скриптом из корня Android-проекта:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\download_vosk_ru_model.ps1
```

Скрипт скачивает `vosk-model-small-ru-0.22`, заменяет каталог `app/src/main/assets/model-ru` и использует временную папку `.download`, которая исключена из Git.

### Подключение к ESP32-C6

#### Через домашнюю Wi-Fi сеть

1. Укажите SSID и пароль сети в прошивке ESP.
2. Подключите телефон к той же сети.
3. Нажмите `Discover`. Приложение отправит UDP broadcast-запрос.
4. Проверьте найденный IP и порт `3333`.
5. Нажмите `Connect`.
6. Нажмите кнопку D1 на ESP для начала записи, говорите и нажмите её повторно для завершения.

#### Через точку доступа ESP

Если station SSID в прошивке пустой, подключите телефон напрямую:

```text
SSID: ESP32C6_MIC
Password: 12345678
ESP IP: 192.168.4.1
TCP: 3333
```

Введите `192.168.4.1` вручную или используйте `Discover`, затем нажмите `Connect`.

### Микрофон телефона

Кнопка `Phone mic` запускает Android SpeechRecognizer. На поддерживаемых устройствах приложение предпочитает on-device recognition; иначе используется системный распознаватель, который может зависеть от сервисов устройства и интернет-соединения. Повторное нажатие останавливает прослушивание.

### Локальная LLM на телефоне

Нажмите `Pick model` и выберите файл модели из хранилища устройства. Приложение копирует его во внутренний каталог и определяет формат по содержимому.

Поддерживаются:

- LiteRT-LM `.litertlm` с заголовком `LITERTLM`;
- MediaPipe LLM Inference `.task` bundle.

Файлы `web.task`/TFLite с сигнатурой `TFL3` не совместимы с текущим загрузчиком. Расширение файла само по себе не гарантирует совместимость. Если модель не загружается, проверьте журнал в нижней части экрана.

Для LiteRT-LM используется CPU backend с четырьмя потоками и контекстом до 2048 токенов. Для MediaPipe установлен предел генерации 1024 токена. Фактическая скорость и возможность загрузки зависят от памяти и процессора устройства.

### Ollama на компьютере

Ollama является резервным backend. В поле `Ollama PC IP` укажите только IP компьютера, например `192.168.1.10`. Приложение обращается к:

```text
http://<PC-IP>:11434/api/chat
```

В коде выбрана модель `gemma4:e2b`. Ollama должна принимать подключения из локальной сети; firewall компьютера должен разрешать TCP-порт `11434`. Не открывайте этот порт в интернет без аутентифицированного reverse proxy.

### Данные и конфиденциальность

- Vosk ASR, локальная LLM и Android TTS могут работать на устройстве.
- Последняя исходная запись ESP сохраняется как `last_esp_raw.wav` во внутреннем каталоге приложения.
- Обработанная запись сохраняется как `last_esp_record.wav` во внутреннем каталоге приложения.
- При использовании Ollama распознанный текст отправляется на указанный компьютер.
- При использовании DuckDuckGo распознанный текст отправляется в DuckDuckGo Instant Answer API.
- Резервное копирование приложения включено в манифесте (`allowBackup=true`); учитывайте это для чувствительных записей и моделей.

### Разрешения

| Разрешение | Причина |
|---|---|
| `INTERNET` | TCP-соединение с ESP, Ollama и DuckDuckGo |
| `ACCESS_NETWORK_STATE` | Проверка сетевого состояния |
| `RECORD_AUDIO` | Режим распознавания через микрофон телефона |

### Диагностика

| Проблема | Что проверить |
|---|---|
| `ESP ASR: model not loaded` | Наличие полного каталога `assets/model-ru`, затем переустановите приложение |
| ESP не найден | Одинаковую Wi-Fi сеть, запрет client isolation, UDP broadcast и порт `3334` |
| TCP connection error | IP ESP, порт `3333`, подключение платы и правила сети |
| Пустой текст Vosk | Уровень микрофона, `MIC_GAIN`, длительность записи и WAV-файлы диагностики |
| Local LLM не загружается | Формат модели, свободную RAM/память и журнал исключения |
| Ollama не отвечает | IP ПК, модель `gemma4:e2b`, порт `11434`, firewall и сетевой bind Ollama |
| Нет озвучивания | Наличие и язык Android TTS engine, громкость мультимедиа |

### Известные ограничения

- Интерфейс и системный prompt ориентированы прежде всего на русский язык.
- UDP discovery может не работать в гостевых и изолированных Wi-Fi сетях.
- Качество Vosk ограничено качеством сигнала аналогового микрофона ESP.
- Совместимость локальных LLM зависит от конкретного файла модели и аппаратных возможностей телефона.
- Release-сборка пока не включает minification и production signing configuration.

---

## English

### Purpose

ESP Voice is an Android client for a voice assistant built around the Seeed Studio XIAO ESP32-C6. The application receives microphone audio from the board over Wi-Fi, recognizes Russian speech locally with Vosk, produces an answer, and speaks it through Android TextToSpeech.

The app also supports the phone microphone, on-device LLMs, and Ollama running on a computer as a fallback. A PC is not required when a compatible local model is loaded on the phone or when the built-in commands and DuckDuckGo fallback are sufficient.

### Technical summary

| Property | Value |
|---|---|
| Application ID | `com.example.espvoiceassistant` |
| Version | `0.1` (`versionCode 1`) |
| Minimum Android version | Android 8.0 / API 26 |
| Target / Compile SDK | API 35 |
| Language and UI | Kotlin, Jetpack Compose, Material 3 |
| Java toolchain | Java 17 |
| ESP transport | TCP port `3333` |
| ESP discovery | UDP broadcast port `3334` |
| Audio format | mono PCM, 16 kHz, signed 16-bit LE |
| ESP ASR | Vosk Android `0.3.75` |
| On-device LLM | LiteRT-LM or MediaPipe LLM Inference |

### Request pipeline

```text
ESP microphone
  → TCP PCM stream
  → filtering and normalization
  → local Vosk ASR
  → answer provider
  → Android TextToSpeech

Phone microphone
  → Android SpeechRecognizer
  → answer provider
  → Android TextToSpeech
```

Answer providers are evaluated in this order:

1. Built-in time and date commands.
2. An on-device `.litertlm` model or compatible MediaPipe `.task` bundle.
3. Ollama on a PC when its IP address is configured.
4. DuckDuckGo Instant Answer when internet access is available.
5. An informational message when no suitable answer is available.

### Requirements

- Android Studio compatible with Android Gradle Plugin 8.13.2.
- JDK 17.
- Android SDK 35.
- A phone or tablet running Android 8.0 or later.
- An ESP32-C6 flashed with the firmware from `../esp_serial_mic_c6`.
- The phone and ESP on the same Wi-Fi network, or the phone connected to the ESP access point.
- Microphone permission for `Phone mic` mode.
- Sufficient device storage and memory for the selected local LLM.

### Build and run

The repository does not contain a complete Gradle Wrapper, so Android Studio is the recommended build environment.

1. Open `android_voice_assistant` as a standalone Android Studio project.
2. Select JDK 17 for Gradle.
3. Install Android SDK 35 if prompted.
4. Wait for Gradle Sync and dependency downloads to finish.
5. Connect an Android device with USB debugging enabled.
6. Run the `app` configuration.
7. Grant microphone access on first launch.

### Vosk model

The Russian Vosk model is stored in:

```text
app/src/main/assets/model-ru/
```

It is copied to the app's internal storage while ASR is initialized. Successful initialization is shown as `ESP ASR: Vosk local ready`.

If the model is missing or damaged, restore it from the Android project root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\download_vosk_ru_model.ps1
```

The script downloads `vosk-model-small-ru-0.22`, replaces `app/src/main/assets/model-ru`, and uses the temporary `.download` directory, which is ignored by Git.

### Connecting to the ESP32-C6

#### Home Wi-Fi network

1. Configure the network SSID and password in the ESP firmware.
2. Connect the phone to the same network.
3. Tap `Discover` to send a UDP broadcast query.
4. Verify the detected IP address and port `3333`.
5. Tap `Connect`.
6. Press the D1 button on the ESP to start recording, speak, and press it again to stop.

#### ESP access point

When the station SSID in the firmware is empty, connect the phone directly:

```text
SSID: ESP32C6_MIC
Password: 12345678
ESP IP: 192.168.4.1
TCP: 3333
```

Enter `192.168.4.1` manually or use `Discover`, then tap `Connect`.

### Phone microphone

The `Phone mic` button starts Android SpeechRecognizer. The app prefers on-device recognition on supported devices; otherwise it uses the system recognizer, which may depend on device services and internet access. Tap the button again to stop listening.

### On-device LLM

Tap `Pick model` and select a model file from device storage. The app copies the file to its internal directory and detects the format from its contents.

Supported formats:

- LiteRT-LM `.litertlm` files with the `LITERTLM` header;
- MediaPipe LLM Inference `.task` bundles.

`web.task`/TFLite files with a `TFL3` signature are not compatible with the current loader. A filename extension alone does not guarantee compatibility. Review the in-app log when a model fails to load.

LiteRT-LM uses a four-thread CPU backend and a context limit of 2048 tokens. MediaPipe generation is limited to 1024 tokens. Actual performance and model compatibility depend on device memory and CPU capabilities.

### Ollama on a computer

Ollama is a fallback backend. Enter only the computer's IP address in `Ollama PC IP`, for example `192.168.1.10`. The app connects to:

```text
http://<PC-IP>:11434/api/chat
```

The configured model is `gemma4:e2b`. Ollama must accept local-network connections, and the computer firewall must allow TCP port `11434`. Do not expose this port to the public internet without an authenticated reverse proxy.

### Data and privacy

- Vosk ASR, an on-device LLM, and Android TTS can run locally on the phone.
- The latest raw ESP recording is stored as `last_esp_raw.wav` in internal app storage.
- The processed recording is stored as `last_esp_record.wav` in internal app storage.
- When Ollama is used, recognized text is sent to the configured computer.
- When DuckDuckGo fallback is used, recognized text is sent to the DuckDuckGo Instant Answer API.
- Application backup is enabled in the manifest (`allowBackup=true`); account for this when handling sensitive recordings or models.

### Permissions

| Permission | Purpose |
|---|---|
| `INTERNET` | TCP access to the ESP, Ollama, and DuckDuckGo |
| `ACCESS_NETWORK_STATE` | Network-state checks |
| `RECORD_AUDIO` | Speech recognition through the phone microphone |

### Troubleshooting

| Problem | Check |
|---|---|
| `ESP ASR: model not loaded` | Verify the complete `assets/model-ru` directory, then reinstall the app |
| ESP is not discovered | Same Wi-Fi network, client isolation, UDP broadcast, and port `3334` |
| TCP connection error | ESP IP, port `3333`, board connectivity, and network rules |
| Empty Vosk result | Microphone level, `MIC_GAIN`, recording length, and diagnostic WAV files |
| Local LLM does not load | Model format, available RAM/storage, and the exception in the app log |
| Ollama does not respond | PC IP, `gemma4:e2b`, port `11434`, firewall, and Ollama network binding |
| No spoken response | Installed Android TTS engine, language data, and media volume |

### Known limitations

- The interface and system prompt primarily target Russian usage.
- UDP discovery may fail on guest or client-isolated Wi-Fi networks.
- Vosk accuracy is constrained by the ESP analog microphone signal quality.
- Local LLM compatibility depends on the exact model file and the phone hardware.
- The release build currently has no minification or production signing configuration.
