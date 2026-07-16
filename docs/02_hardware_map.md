# 02. Hardware Map

## Components

| Component | Role | Interface |
|---|---|---|
| ESP32-WROOM-32 DevKit | main controller | core |
| ST7789 240×240 TFT | face/menu/video | SPI |
| capacitive touch button | quick action | GPIO/touch |
| mouse wheel / encoder | menu navigation | GPIO |
| SHT31 | temp/humidity | I2C |
| MPU6050 | motion/tilt/wake-up | I2C |
| GPS module + antenna | next-version Outdoor Mode | UART |
| small speaker | sound | I2S via amp |
| INMP441 microphone | short online voice request | I2S |
| SD card | video/audio/files | SPI |

## Bus allocation

```text
SPI bus:
- ST7789 display
- SD card
each with separate CS

I2C bus:
- SHT31
- MPU6050

UART, next version:
- GPS/GNSS

I2S:
- speaker amplifier
- optional microphone

GPIO:
- encoder A/B/button
- touch button
- display control pins
```

## Notes

- ESP32-WROOM-32 is the target board for the desktop product.
- The active ESP32 DevKit prototype uses the display, SHT31, three-contact
  encoder, and TTP223. The MPU6050 driver and reactions are ready; physical
  verification is deferred until the module is soldered.
- Audio loopback is available through MAX98357A and INMP441 on shared I2S
  clocks. It is active only inside `AUDIO LOOPBACK`.
- ST7789 is SPI, not I2C.
- SHT31 and MPU6050 can share I2C.
- GPS antenna alone is not enough; a GNSS receiver module is required.
- Speaker should not be connected directly to GPIO. Use amplifier.
- BLE is not suitable for video streaming. Use SD card for Mini TV.
- Product online functions use Wi-Fi/HTTPS; BLE remains diagnostic only.
