# CoPet Pilot

Настольный embedded-робот-компаньон на **ESP32-S3**: экран-лицо, меню, фокус-таймер,
датчики температуры/влажности и движения, звук, SD-карта для локального медиа и
BLE-мост к телефону для интернет-функций (погода, голос, AI).

Полное техническое задание — [CoPet_Pilot_general_spec.md](CoPet_Pilot_general_spec.md).

> **Статус:** скелет проекта. Все исходники в `main/` — заглушки без реализации.

## Структура

```text
main/
  app_main.c              — точка входа
  core/                   — state machine, события, конфиг
  modes/                  — Desk, Focus, Phone Bridge, Mini TV, Outdoor, Settings
  ui/                     — экраны: меню, лицо, таймер, погода, TV, outdoor
  drivers/                — ST7789, кнопка, энкодер, SHT31, MPU6050, GPS, SD, I2S
  services/               — BLE, media, focus, sensors, storage, power
```

Соответствие модулей режимам и железу описано в разделах 4–5 и 14 ТЗ.

## Аппаратная платформа (MVP)

- ESP32-S3 (желательно с PSRAM)
- ST7789 240×240 TFT (SPI)
- Rotary encoder / колесо от мыши + сенсорная кнопка
- SHT31 (I2C), MPU6050 (I2C)
- Динамик через усилитель (MAX98357A, I2S)
- SD-карта (SPI)

GPS/GNSS и микрофон — после MVP (ревизии 4–5, см. раздел 17 ТЗ).

## Сборка

Проект рассчитан на **ESP-IDF 5.x**. Сборочная конфигурация (`CMakeLists.txt`,
`sdkconfig.defaults`, карта пинов) пока не добавлена — на текущем этапе это только
скелет структуры. После добавления сборки:

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash monitor
```
