# 01. CoPet Pilot Architecture

## Concept

CoPet Pilot — настольный embedded-компаньон на ESP32-S3 с экраном, меню, сенсорным вводом, датчиками, звуком, SD-картой, BLE-связью с телефоном и outdoor GPS-режимом.

## High-level architecture

```text
                    ┌────────────────────┐
                    │     Phone App       │
                    │ Wi-Fi / AI / Voice  │
                    │ Weather / Settings  │
                    └─────────┬──────────┘
                              │ BLE
                              ▼
┌────────────────────────────────────────────────┐
│                  ESP32-S3                      │
│                                                │
│  SPI  ── ST7789 TFT display                    │
│  SPI  ── SD card                               │
│  I2C  ── SHT31 + MPU6050                       │
│  UART ── GPS/GNSS module                       │
│  GPIO ── rotary encoder + touch button         │
│  I2S  ── speaker amplifier                     │
│  I2S  ── microphone, optional                  │
│  BLE  ── Phone Bridge                          │
└────────────────────────────────────────────────┘
```

## Modes

| Mode | Purpose |
|---|---|
| Desk Mode | обычная жизнь на столе |
| Focus Mode | учебный таймер |
| Phone Bridge | связь с телефоном |
| Mini TV | видео/звук с SD |
| Outdoor Mode | скорость/дистанция через GPS |
| Settings | яркость, звук, параметры |

## State machine

```text
BOOT
  ↓
MENU
  ├── DESK_MODE
  ├── FOCUS_MODE
  ├── PHONE_BRIDGE_MODE
  ├── MINI_TV_MODE
  ├── OUTDOOR_MODE
  └── SETTINGS
```

## Principle

ESP32-S3 — тело устройства: экран, меню, сенсоры, звуки, локальная логика.  
Телефон — интернет: погода, AI, голосовой ввод, сложные запросы.
