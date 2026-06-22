# ESP32-C6 Voice Assistant

[Русский](#русский) · [English](#english)

## Русский

### О проекте

ESP32-C6 Voice Assistant — экспериментальная система голосового управления, в которой плата Seeed Studio XIAO ESP32-C6 захватывает звук с аналогового микрофона и передаёт PCM-аудио клиенту. Основной клиент — Android-приложение с локальным распознаванием речи, поддержкой локальных LLM и синтезом речи. Для разработки и диагностики также предусмотрен опциональный Python-клиент для Windows.

Проект не требует облачного ASR: речь с ESP распознаётся на телефоне через Vosk или на ПК через faster-whisper. Генерация ответа может выполняться локально на Android, через Ollama в локальной сети либо через DuckDuckGo Instant Answer как резервный источник.

### Архитектура

```text
Аналоговый микрофон + кнопка
              │
              ▼
       Seeed XIAO ESP32-C6
       ADC → PCM 16 кГц / 16 бит
              │ TCP :3333 / Wi-Fi
              ▼
       Android-приложение
       Vosk ASR → выбор источника ответа
       → Android TextToSpeech

Опционально: ESP32-C6 → USB Serial или Wi-Fi → Python-клиент →
faster-whisper → Ollama → pyttsx3
```

### Возможности

- Захват монофонического PCM-аудио: 16 кГц, signed 16-bit little-endian.
- Запуск и остановка записи физической кнопкой на ESP32-C6.
- Передача событий и аудио по TCP через Wi-Fi.
- Поиск платы в локальной сети через UDP broadcast.
- Резервная точка доступа ESP32-C6 для прямого подключения телефона.
- Локальное распознавание аудио ESP через Vosk на Android.
- Дополнительное распознавание через микрофон телефона.
- Локальные Android LLM в форматах LiteRT-LM `.litertlm` и совместимых MediaPipe `.task` bundles.
- Опциональная Gemma через Ollama на ПК.
- Озвучивание ответа через Android TextToSpeech или TTS компьютера.
- Сохранение исходной и обработанной WAV-записи для диагностики.
- OLED-анимация глаз на подключённом дисплее.

### Состав репозитория

| Путь | Назначение |
|---|---|
| `esp_serial_mic_c6/` | Прошивка ESP-IDF для XIAO ESP32-C6 |
| `android_voice_assistant/` | Основное Android-приложение |
| `assistant_ui.py` | Графический Python-клиент с Serial/Wi-Fi подключением |
| `assistant.py` | Консольный Python-клиент для Serial |
| `log_esp_serial.py`, `monitor.py` | Диагностика последовательного протокола |
| `analyze_wav.py`, `test_*.py` | Анализ и проверка обработки аудио |

### Подключение оборудования

| Компонент | XIAO ESP32-C6 |
|---|---|
| Микрофон `AOUT` | D0 / GPIO0 / ADC1_CH0 |
| Микрофон `VCC` | 3V3 |
| Микрофон `GND` | GND |
| Кнопка запуска/остановки записи | D1 / GPIO1 → GND |

Вход `DOUT` аналогового микрофонного модуля не используется. Для кнопки включена внутренняя подтяжка к питанию.

### Прошивка ESP32-C6

Требования:

- ESP-IDF 5.5.x;
- Seeed Studio XIAO ESP32-C6;
- USB-кабель с передачей данных.

Перед сборкой настройте `WIFI_STA_SSID` и `WIFI_STA_PASS` в `esp_serial_mic_c6/main/serial_mic_c6_main.c`. Не добавляйте реальные пароли в Git. Если оставить SSID пустым, используйте резервную сеть платы:

```text
SSID: ESP32C6_MIC
Password: 12345678
ESP address: 192.168.4.1
TCP port: 3333
```

Сборка и прошивка:

```powershell
cd esp_serial_mic_c6
idf.py set-target esp32c6
idf.py build
idf.py -p COM5 flash monitor
```

Замените `COM5` на порт своей платы.

### Android-приложение

Откройте каталог `android_voice_assistant` в Android Studio, дождитесь синхронизации Gradle и запустите модуль `app` на устройстве с Android 8.0 или новее. Полное руководство находится в [Android README](android_voice_assistant/README.md).

### Python-клиент для ПК

Python-клиент является дополнительным инструментом и не требуется Android-приложению.

Установите зависимости:

```powershell
python -m pip install numpy scipy pyserial requests faster-whisper pyttsx3 sounddevice
```

Запуск интерфейса:

```powershell
python assistant_ui.py
```

Запуск консольного клиента через USB Serial:

```powershell
python assistant.py COM5
```

В коде ПК-клиента настроена модель Ollama `gemma4:e2b` на `http://localhost:11434`. Ollama должна быть запущена отдельно, а модель — предварительно установлена.

### Сетевой протокол

ESP передаёт бинарные кадры следующего формата:

| Тип | Формат |
|---|---|
| Аудио | `A5 5A 01 lenLo lenHi <PCM int16 LE>` |
| Начало записи | `A5 5A 02 01` |
| Конец записи | `A5 5A 02 00` |

TCP-сервер работает на порту `3333`. UDP-discovery использует порт `3334`: клиент отправляет `ESP32C6_MIC?`, после чего плата сообщает IP и TCP-порт.

### Ограничения

- Качество распознавания напрямую зависит от аналогового микрофона, уровня сигнала и параметра `MIC_GAIN`.
- В репозитории нет полного Gradle Wrapper; Android-проект следует собирать через Android Studio либо добавить wrapper отдельно.
- ESP-IDF, Android SDK, модели LLM и Ollama не входят в репозиторий.
- DuckDuckGo fallback требует интернет-соединения и передаёт распознанный текст внешнему сервису.
- Лицензия проекта пока не определена.

---

## English

### Overview

ESP32-C6 Voice Assistant is an experimental voice-control system in which a Seeed Studio XIAO ESP32-C6 captures audio from an analog microphone and streams PCM data to a client. The primary client is an Android application with local speech recognition, on-device LLM support, and speech synthesis. An optional Windows Python client is included for development and diagnostics.

The system does not require cloud ASR. Audio from the ESP can be recognized locally with Vosk on Android or faster-whisper on a PC. Answers can be generated by an on-device Android model, Ollama on the local network, or DuckDuckGo Instant Answer as a fallback.

### Architecture

```text
Analog microphone + button
              │
              ▼
       Seeed XIAO ESP32-C6
       ADC → 16 kHz / 16-bit PCM
              │ TCP :3333 / Wi-Fi
              ▼
       Android application
       Vosk ASR → answer provider
       → Android TextToSpeech

Optional: ESP32-C6 → USB Serial or Wi-Fi → Python client →
faster-whisper → Ollama → pyttsx3
```

### Features

- Mono PCM capture at 16 kHz, signed 16-bit little-endian.
- Physical button that toggles recording on and off.
- Audio and event streaming over Wi-Fi TCP.
- UDP broadcast discovery on the local network.
- ESP32-C6 fallback access point for direct phone connections.
- Local Vosk recognition of ESP audio on Android.
- Optional recognition through the phone microphone.
- On-device Android LLMs in LiteRT-LM `.litertlm` and compatible MediaPipe `.task` bundle formats.
- Optional Gemma inference through Ollama on a PC.
- Speech output through Android TextToSpeech or desktop TTS.
- Raw and processed WAV recordings for diagnostics.
- Animated eyes on a connected OLED display.

### Repository layout

| Path | Purpose |
|---|---|
| `esp_serial_mic_c6/` | ESP-IDF firmware for the XIAO ESP32-C6 |
| `android_voice_assistant/` | Primary Android application |
| `assistant_ui.py` | Graphical Python client with Serial and Wi-Fi transports |
| `assistant.py` | Serial-based command-line Python client |
| `log_esp_serial.py`, `monitor.py` | Serial protocol diagnostics |
| `analyze_wav.py`, `test_*.py` | Audio analysis and processing checks |

### Hardware wiring

| Component | XIAO ESP32-C6 |
|---|---|
| Microphone `AOUT` | D0 / GPIO0 / ADC1_CH0 |
| Microphone `VCC` | 3V3 |
| Microphone `GND` | GND |
| Start/stop recording button | D1 / GPIO1 → GND |

The analog microphone module's `DOUT` pin is not used. The button uses the internal pull-up resistor.

### ESP32-C6 firmware

Requirements:

- ESP-IDF 5.5.x;
- Seeed Studio XIAO ESP32-C6;
- a data-capable USB cable.

Before building, set `WIFI_STA_SSID` and `WIFI_STA_PASS` in `esp_serial_mic_c6/main/serial_mic_c6_main.c`. Never commit real credentials. If the station SSID is empty, connect through the board's fallback network:

```text
SSID: ESP32C6_MIC
Password: 12345678
ESP address: 192.168.4.1
TCP port: 3333
```

Build and flash:

```powershell
cd esp_serial_mic_c6
idf.py set-target esp32c6
idf.py build
idf.py -p COM5 flash monitor
```

Replace `COM5` with the board's serial port.

### Android application

Open `android_voice_assistant` in Android Studio, allow Gradle synchronization to finish, and run the `app` module on a device running Android 8.0 or later. See the [Android README](android_voice_assistant/README.md) for complete instructions.

### Optional desktop client

The Python client is an additional development tool and is not required by the Android application.

Install its dependencies:

```powershell
python -m pip install numpy scipy pyserial requests faster-whisper pyttsx3 sounddevice
```

Start the graphical client:

```powershell
python assistant_ui.py
```

Start the command-line client over USB Serial:

```powershell
python assistant.py COM5
```

The desktop client is configured to use the Ollama model `gemma4:e2b` at `http://localhost:11434`. Ollama must be running separately and the model must already be installed.

### Network protocol

The ESP sends binary frames in the following format:

| Type | Format |
|---|---|
| Audio | `A5 5A 01 lenLo lenHi <PCM int16 LE>` |
| Recording started | `A5 5A 02 01` |
| Recording stopped | `A5 5A 02 00` |

The TCP server listens on port `3333`. UDP discovery uses port `3334`: a client broadcasts `ESP32C6_MIC?`, and the board responds with its IP address and TCP port.

### Limitations

- Recognition quality depends on the analog microphone, signal level, and the firmware `MIC_GAIN` setting.
- The repository does not contain a complete Gradle Wrapper; build the Android project with Android Studio or add the wrapper separately.
- ESP-IDF, Android SDK components, LLM model files, and Ollama are not included.
- The DuckDuckGo fallback requires internet access and sends recognized text to an external service.
- No project license has been defined yet.
