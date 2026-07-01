# CoPet Pilot — общее техническое задание

## 1. Краткое описание проекта

**CoPet Pilot** — это настольный embedded-робот-компаньон на базе **ESP32-S3**. Устройство работает как маленький интерактивный персонаж на столе: показывает лицо, меню, фокус-таймер, температуру/влажность, реагирует на движение, воспроизводит звуки и короткие видео/анимации.  

Дополнительно устройство может переходить в **Phone Bridge Mode** и подключаться к мобильному приложению по **BLE**. Телефон в этом режиме отвечает за интернет-функции: погоду, голосовой ввод, AI-вопросы/ответы, синхронизацию настроек и данных.

В расширенной версии устройство может использовать **GPS/GNSS-модуль с антенной** для Outdoor Mode: измерение скорости, дистанции, направления движения и мини-лог активности при креплении на рюкзак/сумку/велосипед.

---

## 2. Главная идея

Проект — не «чат-бот на ESP32», а физический companion-device с характером.

> **CoPet Pilot — настольный companion-device с режимами, сенсорами, экраном, звуком и телефонным мостом для AI/интернета.**

Разделение ответственности: ESP32-S3 держит всё локальное поведение (UI, режимы, датчики, звук, медиа, BLE), телефон закрывает интернет-функции (погода, голос, AI). Подробности — в разделах 5 и 11.

---

## 3. Основные режимы устройства

| Режим | Назначение |
|---|---|
| **Desk Mode** | основной настольный режим |
| **Focus Mode** | режим учёбы/работы с таймером |
| **Phone Bridge Mode** | связь с телефоном по BLE |
| **Mini TV Mode** | воспроизведение видео/звуков с SD-карты |
| **Outdoor Mode** | скорость/движение через GPS и IMU |
| **Settings Mode** | настройки яркости, звука, BLE, темы |

---

## 4. Аппаратные компоненты

| Компонент | Использовать? | Комментарий |
|---|---:|---|
| **ESP32-S3** | да | основной мозг устройства |
| **ST7789 240×240 TFT** | да | экран для лица, меню, видео |
| **сенсорная кнопка** | да | быстрый выбор/старт режима |
| **колесо от мыши / энкодер** | да | прокрутка меню |
| **SHT31 температура/влажность** | да | Desk Mode / room comfort |
| **MPU6050** | да | движение, наклон, wake-up |
| **GPS-модуль + антенна** | да, но позже | скорость на улице |
| **маленький динамик** | да | через усилитель |
| **микрофон** | можно, но не в MVP | сначала голос через телефон |
| **SD-карта** | да | видео, звуки, картинки, логи |

---

## 5. Роль каждого компонента

### 5.1 ESP32-S3

ESP32-S3 — главный контроллер устройства.

Отвечает за:

- запуск прошивки;
- state machine режимов;
- отрисовку UI;
- чтение кнопок и энкодера;
- работу с датчиками;
- BLE-связь с телефоном;
- работу с SD-картой;
- воспроизведение звука;
- управление экраном;
- хранение настроек в NVS/flash.

Желательно использовать плату ESP32-S3 с **PSRAM**, потому что проект включает экран, анимации, буферы изображений, BLE и потенциально аудио/медиа.

---

### 5.2 ST7789 240×240 TFT

Экран используется как лицо и основной интерфейс CoPet Pilot.

Назначение:

- лицо робота;
- меню;
- Focus Mode timer;
- погода;
- комнатная температура/влажность;
- статусы BLE;
- outdoor speed screen;
- Mini TV Mode;
- короткие AI-ответы.

Интерфейс: **SPI**.

Типичные пины модуля:

```text
GND
VCC
SCK
SDA / MOSI
RES
DC
BLK
```

---

### 5.3 Сенсорная кнопка

Сенсорная кнопка используется как быстрый физический control element.

Предлагаемая логика:

| Действие | Функция |
|---|---|
| короткое касание | выбрать / подтвердить |
| долгое касание | назад / выход |
| двойное касание | быстрый Focus Mode |
| удержание 5 сек | sleep / выключить экран |

---

### 5.4 Колесо от мыши / энкодер

Колесо используется для удобной навигации по меню.

Предлагаемая логика:

| Действие | Функция |
|---|---|
| прокрутка вверх | предыдущий пункт меню |
| прокрутка вниз | следующий пункт меню |
| нажатие колеса | выбрать пункт |
| удержание | вернуться назад |

Колесо от мыши обычно работает как rotary encoder:

```text
A phase
B phase
Button
GND
VCC / pull-up
```

---

### 5.5 SHT31 температура/влажность

SHT31 используется для Desk Mode и функции “room comfort”.

Примеры отображения:

```text
Room: 24°C
Humidity: 48%
CoPet feels comfy
```

Поведение CoPet:

| Условие | Реакция |
|---|---|
| комфортно | happy idle |
| жарко | tired face |
| холодно | shivering animation |
| сухо | asks for water |
| влажно | uncomfortable face |

Интерфейс: **I2C**.

---

### 5.6 MPU6050

MPU6050 используется для движения, наклона и wake-up логики.

Назначение:

- определить, что устройство взяли в руки;
- определить наклон;
- детектировать тряску;
- разбудить экран по движению;
- отличать desk state от carried state;
- использовать motion events для анимаций;
- помогать Outdoor Mode.

Важно: **MPU6050 не должен быть основным источником скорости**. Скорость по акселерометру быстро накапливает ошибку. Для реальной скорости лучше использовать GPS/GNSS.

Интерфейс: **I2C**.

---

### 5.7 GPS/GNSS-модуль + антенна

GPS/GNSS используется во второй аппаратной ревизии для Outdoor Mode.

Назначение:

- скорость;
- дистанция;
- координаты;
- направление движения;
- точное время;
- количество спутников;
- last known location;
- outdoor activity log.

Схема:

```text
GPS/GNSS antenna
        ↓
GPS/GNSS receiver module
        ↓ UART
ESP32-S3
```

Важно: одна антенна без GNSS-модуля не даст данные. Нужен именно модуль-приёмник.

---

### 5.8 Динамик

Динамик используется для:

- звуков питомца;
- feedback-звуков меню;
- завершения Focus Mode;
- Mini TV audio;
- коротких voice/audio responses.

Динамик нельзя нормально подключать напрямую к GPIO ESP32. Нужен усилитель.

Рекомендуемые варианты:

| Усилитель | Комментарий |
|---|---|
| **MAX98357A** | хороший I2S mono amplifier |
| **PAM8302** | простой mono amp |
| **PAM8403** | простой stereo amp |

Предпочтительный вариант: **MAX98357A по I2S**.

---

### 5.9 Микрофон

Микрофон не входит в MVP. В первой версии голос лучше делать через телефон.

Поток для голосового ассистента в MVP:

```text
User voice
   ↓
Phone microphone
   ↓
Speech-to-text
   ↓
AI / internet
   ↓
BLE
   ↓
CoPet screen + sound
```

Позже можно добавить I2S MEMS microphone:

- INMP441;
- ICS-43434;
- SPH0645.

---

### 5.10 SD-карта

SD-карта нужна для Mini TV Mode и хранения ресурсов.

На SD-карте можно хранить:

- короткие видео;
- картинки;
- анимации;
- звуки;
- темы;
- focus logs;
- outdoor logs;
- cached phone data.

Рекомендуемый формат для видео:

```text
MJPEG 240×240, 10–15 fps
+ WAV / MP3 audio
```

Не рекомендуется пытаться проигрывать обычный YouTube/MP4/H.264 напрямую на ESP32-S3.

---

## 6. Общая аппаратная архитектура

```text
                    ┌────────────────────┐
                    │     Phone App      │
                    │ Wi-Fi / Weather    │
                    │ Voice / AI         │
                    └─────────┬──────────┘
                              │ BLE
                              ↓
┌────────────────────────────────────────────────┐
│                  ESP32-S3                      │
│                                                │
│  SPI  ── ST7789 240×240 TFT                    │
│  SPI  ── SD card module                        │
│  I2C  ── SHT31 temperature/humidity            │
│  I2C  ── MPU6050 IMU                           │
│  UART ── GPS/GNSS module                       │
│  GPIO ── capacitive touch button               │
│  GPIO ── rotary encoder / mouse wheel          │
│  I2S  ── audio amplifier → speaker             │
│  I2S  ── microphone, optional                  │
└────────────────────────────────────────────────┘
```

---

## 7. Интерфейсы подключения

| Модуль | Интерфейс | Назначение |
|---|---|---|
| ST7789 TFT | SPI | экран |
| SD card | SPI | медиа и логи |
| SHT31 | I2C | температура/влажность |
| MPU6050 | I2C | движение/наклон |
| GPS/GNSS | UART | скорость/координаты |
| энкодер | GPIO interrupt | меню |
| сенсорная кнопка | GPIO / touch | управление |
| динамик | I2S + amplifier | звук |
| микрофон | I2S | голос, позже |
| телефон | BLE | Phone Bridge |

---

## 8. Предлагаемое меню

```text
CoPet Pilot

> Desk Mode
  Focus Mode
  Phone Bridge
  Mini TV
  Outdoor Pilot
  Settings
```

Управление:

| Управление | Действие |
|---|---|
| колесо вверх/вниз | прокрутка пунктов |
| нажатие колеса | выбрать |
| сенсорная кнопка | быстрый выбор / Focus |
| долгое касание | назад |
| удержание | sleep |

---

## 9. Desk Mode

Desk Mode — основной режим, когда устройство стоит на столе и питается от USB.

Функции:

- лицо CoPet;
- idle-анимации;
- отображение времени;
- отображение температуры/влажности;
- реакция на движение;
- sleep при отсутствии активности;
- быстрый переход в Focus Mode;
- звуки/эмоции.

Пример экрана:

```text
12:45
24°C  48%

CoPet is comfy :)
```

---

## 10. Focus Mode

Focus Mode — режим учёбы/работы.

Функции:

- таймер фокуса;
- пауза/продолжение;
- завершение сессии;
- короткий звук окончания;
- статистика сессий;
- геймификация питомца.

Базовые параметры:

```text
Focus: 25 min
Break: 5 min
Cycles: 4
```

Пример экрана:

```text
FOCUS
23:41 left

CoPet studies with you
```

---

## 11. Phone Bridge Mode

Phone Bridge Mode включается вручную через меню.

Логика:

```text
User selects Phone Bridge
        ↓
ESP32 starts BLE advertising
        ↓
Phone app connects
        ↓
Phone sends data/events
        ↓
ESP32 displays response
```

Функции через телефон:

| Функция | Где выполняется |
|---|---|
| интернет | телефон |
| погода | телефон |
| voice input | телефон |
| speech-to-text | телефон |
| AI question-answering | телефон |
| настройки | телефон + ESP32 |
| отображение результата | ESP32 |

Пример погоды:

```json
{
  "type": "weather_update",
  "city": "Riga",
  "temp": 18,
  "condition": "rain",
  "text": "Take umbrella"
}
```

Пример AI-ответа:

```json
{
  "type": "assistant_answer",
  "text": "Better study 25 minutes first.",
  "mood": "motivating"
}
```

---

## 12. Mini TV Mode

Mini TV Mode — режим проигрывания локальных коротких видео/анимаций с SD-карты.

Рекомендуемый подход:

```text
SD card
  ↓
MJPEG frames
  ↓
ESP32-S3
  ↓
ST7789 display
```

Для звука:

```text
SD card audio
  ↓
ESP32-S3 I2S
  ↓
MAX98357A
  ↓
speaker
```

Не рекомендуется:

- стримить YouTube напрямую на ESP32;
- передавать видео по BLE;
- начинать с MP4/H.264;
- делать полноценный видеоплеер в первой версии.

Рекомендуемый формат:

```text
/video/focus_intro.mjpeg
/video/rain_loop.mjpeg
/video/copet_idle.mjpeg
/audio/focus_intro.wav
/audio/rain_loop.wav
```

---

## 13. Outdoor Mode

Outdoor Mode используется, когда устройство снято со стола и закреплено на рюкзак/сумку/велосипед.

Функции:

- скорость;
- дистанция;
- состояние движения;
- ходьба/езда;
- направление движения;
- last known location;
- outdoor animation.

Источники данных:

| Данные | Источник |
|---|---|
| скорость | GPS/GNSS |
| координаты | GPS/GNSS |
| дистанция | GPS/GNSS |
| движение/тряска | MPU6050 |
| подняли/положили | MPU6050 |
| outdoor state | GPS + MPU6050 |

Пример экрана:

```text
Outdoor Pilot
Speed: 12.6 km/h
Distance: 1.4 km
```

---

## 14. Software architecture

Предлагаемая структура проекта:

```text
main/
  app_main.c

  core/
    app_state_machine.c
    app_events.c
    app_config.c

  modes/
    desk_mode.c
    focus_mode.c
    phone_bridge_mode.c
    mini_tv_mode.c
    outdoor_mode.c
    settings_mode.c

  ui/
    menu_ui.c
    face_ui.c
    focus_ui.c
    weather_ui.c
    mini_tv_ui.c
    outdoor_ui.c

  drivers/
    display_st7789.c
    touch_button.c
    rotary_encoder.c
    sht31_driver.c
    mpu6050_driver.c
    gps_driver.c
    sdcard_driver.c
    audio_i2s.c

  services/
    ble_service.c
    media_service.c
    focus_service.c
    sensor_service.c
    storage_service.c
    power_service.c
```

---

## 15. State machine

Основные состояния:

```text
BOOT
  ↓
MENU
  ↓
DESK_MODE
FOCUS_MODE
PHONE_BRIDGE_MODE
MINI_TV_MODE
OUTDOOR_MODE
SETTINGS_MODE
SLEEP
```

Phone Bridge sub-states:

```text
PHONE_BRIDGE_IDLE
PHONE_BRIDGE_ADVERTISING
PHONE_CONNECTED
PHONE_WEATHER
PHONE_VOICE_ASSISTANT
PHONE_SYNC
```

Outdoor sub-states:

```text
GPS_SEARCHING
GPS_READY
OUTDOOR_IDLE
WALKING
RIDING
GPS_LOST
```

---

## 16. MVP

MVP должен доказать, что устройство работает как самостоятельный embedded-продукт.

### MVP hardware

```text
ESP32-S3
ST7789 TFT
rotary encoder / mouse wheel
capacitive touch button
SHT31
MPU6050
speaker + amplifier
SD card module
USB power
```

### MVP software

```text
- загрузка устройства
- главное меню
- Desk Mode
- Focus Mode
- чтение SHT31
- чтение MPU6050
- базовые анимации лица
- звуки через speaker
- Mini TV с SD-карты
- BLE Phone Bridge basic connect
```

GPS и микрофон можно добавить после MVP.

---

## 17. Ревизии разработки

### Rev 1 — Desk Companion

Цель: собрать настольного робота.

Функции:

- экран;
- меню;
- кнопка;
- энкодер;
- Desk Mode;
- Focus Mode;
- температура/влажность;
- motion reactions;
- базовый звук.

---

### Rev 2 — Mini TV

Цель: добавить локальное медиа.

Функции:

- SD-карта;
- список видео;
- воспроизведение MJPEG;
- WAV/MP3 звук;
- выбор клипа через меню.

---

### Rev 3 — Phone Bridge

Цель: связать устройство с телефоном.

Функции:

- BLE advertising;
- подключение приложения;
- отправка погоды;
- отправка AI-ответа;
- синхронизация настроек;
- синхронизация времени.

---

### Rev 4 — Outdoor Pilot

Цель: добавить скорость и outdoor-режим.

Функции:

- GPS/GNSS module;
- скорость;
- дистанция;
- walking/riding state;
- outdoor animation;
- activity log.

---

### Rev 5 — Voice Assistant

Цель: добавить голосовой ввод.

Первый вариант:

- микрофон телефона;
- speech-to-text на телефоне;
- AI-ответ на телефоне;
- короткий ответ на CoPet.

Позже:

- I2S microphone на устройстве;
- передача аудио/текста через телефон;
- voice wake/action mode.

---

## 18. Критерии готовности

### Устройство считается рабочим MVP, если:

- включается и показывает главное меню;
- энкодером можно прокручивать меню;
- кнопкой можно выбирать режимы;
- Desk Mode показывает лицо/статус;
- SHT31 отдаёт температуру и влажность;
- MPU6050 реагирует на движение/наклон;
- Focus Mode запускает таймер;
- динамик воспроизводит хотя бы короткий звук;
- SD-карта читается;
- Mini TV показывает хотя бы один короткий клип;
- BLE Phone Bridge поднимает advertising и принимает тестовое сообщение.

### Расширенная версия считается готовой, если:

- телефон отправляет погоду;
- телефон отправляет AI-ответ;
- GPS показывает скорость;
- Outdoor Mode считает дистанцию;
- данные логируются;
- корпус позволяет использовать устройство на столе и на рюкзаке.

---

## 19. Что не делать в начале

Не включать в MVP:

- полноценный YouTube-плеер;
- потоковое видео по BLE;
- локальный LLM на ESP32;
- полноценное распознавание речи на ESP32;
- сложный аккумуляторный power-management;
- слишком много режимов сразу;
- большой тачскрин;
- камеру.

---

## 20. Структура прошивки в репозитории

Скелет прошивки лежит в корне репозитория (ESP-IDF project root), исходники — в `main/`:

```text
main/
  app_main.c
  core/       app_state_machine, app_events, app_config
  modes/      desk, focus, phone_bridge, mini_tv, outdoor, settings
  ui/         menu, face, focus, weather, mini_tv, outdoor
  drivers/    display_st7789, touch_button, rotary_encoder, sht31,
              mpu6050, gps, sdcard, audio_i2s
  services/   ble, media, focus, sensor, storage, power
```

Все файлы — заглушки (stubs) без реализации. Порядок реализации — по ревизиям из раздела 17.
