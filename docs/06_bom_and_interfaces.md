# 06. BOM and Interfaces

## Confirmed components

| Component | Use | Interface | Priority |
|---|---|---|---|
| ESP32-S3 | main controller | core | P0 |
| ST7789 240×240 TFT | UI/face | SPI | P0 |
| capacitive touch button | quick input | GPIO/touch | P0 |
| mouse wheel / encoder | menu | GPIO | P0 |
| SHT31 | room comfort | I2C | P0 |
| MPU6050 | motion/tilt | I2C | P0 |
| speaker | sound | I2S amp | P1 |
| SD card | video/audio | SPI | P1 |
| GPS + antenna | speed | UART | P2 |
| microphone | voice | I2S | P3 |

## What each teaches

| Component | You learn |
|---|---|
| ST7789 | SPI, graphics, display buffers |
| encoder | GPIO events, debounce |
| SHT31 | I2C commands, sensor reads |
| MPU6050 | registers, filtering, states |
| speaker | audio pipeline |
| SD card | file I/O, streaming |
| BLE | GATT, protocol, phone bridge |
| GPS | UART, NMEA parsing |
