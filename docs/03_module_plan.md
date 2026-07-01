# 03. Module Plan

## Module order

| Phase | Module | Learning target |
|---:|---|---|
| 1 | ESP32-S3 base | ESP-IDF, CMake, logging |
| 2 | ST7789 display | SPI, display init, drawing |
| 3 | input | GPIO, encoder, debounce |
| 4 | SHT31/MPU6050 | I2C, sensor data |
| 5 | audio | PWM/I2S, speaker amp |
| 6 | SD/Mini TV | filesystem, buffering |
| 7 | BLE | GATT, protocol design |
| 8 | GPS | UART, NMEA, speed |

## Rule

Each module must expose a simple clean API before integration.

Example:

```c
esp_err_t copet_sht31_init(void);
esp_err_t copet_sht31_read(float *temperature_c, float *humidity_percent);
```

Do not let app_main contain all logic.
