# 07. Current ESP32 DevKit Pin Plan

This is the current prototype wiring. The final ESP32-S3 pin plan will be
defined when the board is migrated.

## SPI

| Signal | ESP32-S3 GPIO | Device |
|---|---:|---|
| SCK | GPIO18 | ST7789 |
| MOSI | GPIO5 | ST7789 |
| MISO | not connected | reserved for SD |
| TFT_CS | not present | ST7789 |
| TFT_DC | GPIO16 | ST7789 |
| TFT_RST | GPIO17 | ST7789 |
| TFT_BLK | 3V3 | ST7789 |
| SD_CS | not connected | later phase |

## I2C

| Signal | ESP32-S3 GPIO | Device |
|---|---:|---|
| SDA | GPIO21 | SHT31; MPU6050 deferred |
| SCL | GPIO22 | SHT31; MPU6050 deferred |

## UART GPS

| Signal | ESP32-S3 GPIO | GPS module |
|---|---:|---|
| ESP_RX |  | GPS_TX |
| ESP_TX |  | GPS_RX |

## Input

| Signal | ESP32-S3 GPIO | Device |
|---|---:|---|
| ENCODER_A | GPIO32 | wheel |
| ENCODER_B | GPIO33 | wheel |
| ENCODER_SW | not available | three-contact wheel |
| TOUCH | GPIO13 | TTP223 |

## Audio

| Signal | ESP32-S3 GPIO | Device |
|---|---:|---|
| I2S_BCLK | GPIO26 | MAX98357A BCLK + INMP441 SCK |
| I2S_LRCLK | GPIO25 | MAX98357A LRC + INMP441 WS |
| I2S_DOUT | GPIO27 | MAX98357A DIN |
| I2S_DIN | GPIO34 | INMP441 SD |
