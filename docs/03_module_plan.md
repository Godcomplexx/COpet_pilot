# 03. Module Plan

## Module order

| Phase | Module | Learning target |
|---:|---|---|
| 1 | ESP32-WROOM-32 base | ESP-IDF, CMake, logging |
| 2 | ST7789 display | SPI, display init, drawing |
| 3 | input | GPIO, encoder, debounce |
| 4 | SHT31/MPU6050 | I2C, sensor data |
| 5 | audio | PWM/I2S, speaker amp |
| 6 | architecture refactor | state machine, events, module boundaries |
| 7 | direct Wi-Fi | provisioning, reconnect, SNTP, HTTPS |
| 8 | Online Assistant | bounded JSON, audio streaming, cloud API |
| 9 | SD/Mini TV | filesystem, buffering |
| later | GPS/Outdoor | UART, NMEA, speed |

## Rule

Each module must expose a simple clean API before integration.

Example:

```c
esp_err_t copet_sht31_init(void);
esp_err_t copet_sht31_read(float *temperature_c, float *humidity_percent);
```

Do not let app_main contain all logic.
