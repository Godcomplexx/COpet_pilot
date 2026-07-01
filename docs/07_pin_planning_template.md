# 07. Pin Planning Template

Before wiring, fill this table.

## SPI

| Signal | ESP32-S3 GPIO | Device |
|---|---:|---|
| SCK |  | ST7789 + SD |
| MOSI |  | ST7789 + SD |
| MISO |  | SD only |
| TFT_CS |  | ST7789 |
| TFT_DC |  | ST7789 |
| TFT_RST |  | ST7789 |
| TFT_BLK |  | ST7789 |
| SD_CS |  | SD card |

## I2C

| Signal | ESP32-S3 GPIO | Device |
|---|---:|---|
| SDA |  | SHT31 + MPU6050 |
| SCL |  | SHT31 + MPU6050 |

## UART GPS

| Signal | ESP32-S3 GPIO | GPS module |
|---|---:|---|
| ESP_RX |  | GPS_TX |
| ESP_TX |  | GPS_RX |

## Input

| Signal | ESP32-S3 GPIO | Device |
|---|---:|---|
| ENCODER_A |  | wheel |
| ENCODER_B |  | wheel |
| ENCODER_SW |  | wheel press |
| TOUCH |  | touch button |

## Audio

| Signal | ESP32-S3 GPIO | Device |
|---|---:|---|
| I2S_BCLK |  | amp |
| I2S_LRCLK |  | amp |
| I2S_DOUT |  | amp |
| I2S_DIN |  | microphone |
