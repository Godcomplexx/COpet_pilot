# ESP32 DevKit hardware test

This test firmware is for the **ESP32-WROOM-32 DevKit shown in the photos**.
It is not the ESP32-S2 Saola or the planned ESP32-S3 board.

The test proves three modules:

1. ST7789 displays a status page.
2. SHT3x reports room temperature and humidity.
3. The three-contact roller encoder changes its count and direction.

## Wiring

Disconnect USB power before changing wires.

### ST7789 240x240

| Display | ESP32 DevKit |
|---|---:|
| VCC | 3V3 |
| GND | GND |
| SCK | GPIO18 / D18 |
| SDA (MOSI) | GPIO23 / D23 |
| DC | GPIO16 / D16 |
| RES | GPIO17 / D17 |
| BLK | 3V3 |

This display module has no CS pin.
The GMT130-V1.0 uses an ST7789 controller with a 240x240 visible area and an
80-row vertical memory offset. This module revision also requires SPI mode 2.
The firmware applies both settings.

### SHT3x

| SHT3x | ESP32 DevKit |
|---|---:|
| VIN | 3V3 |
| GND | GND |
| SDA | GPIO21 / D21 |
| SCL | GPIO22 / D22 |

The firmware checks both standard addresses: `0x44` and `0x45`.

### Three-contact roller encoder

| Encoder | ESP32 DevKit |
|---|---:|
| COM | GND |
| A | GPIO32 / D32 |
| B | GPIO33 / D33 |

Do not connect the encoder to 3V3. The firmware enables the ESP32 internal
pull-up resistors.

If the encoder contact order is unknown, try each of its three pads as COM.
Only move the GND wire while USB is disconnected. The correct COM produces
stable changes on both `AB` bits. Swap A and B if left/right is reversed.

## Build and flash

Install ESP-IDF 6.0.x and open its ESP-IDF PowerShell. From the repository root:

```powershell
idf.py set-target esp32
idf.py build
idf.py -p COM5 flash monitor
```

Replace `COM5` with the board's actual COM port. Exit the monitor with
`Ctrl+]`.

Alternatively, use the included helper from ESP-IDF PowerShell:

```powershell
.\start.bat COM5
```

Running `.\start.bat` without an argument prints the available COM ports.

## Expected result

The screen shows:

```text
COPET TEST
SHT OK
TEMP 23.5 C
HUM 45.0
ENC 0
AB 11
ROLL WHEEL
```

Turning the wheel changes `ENC` and displays `LEFT` or `RIGHT`. The serial
monitor prints the same sensor values and every valid encoder transition.

## Common failures

- White screen: check BLK=3V3, DC=GPIO16, RES=GPIO17, SCK=GPIO18 and
  SDA=GPIO23.
- Incorrect colors: change `esp_lcd_panel_invert_color(s_panel, true)` to
  `false`.
- Shifted image: adjust `LCD_X_GAP` or `LCD_Y_GAP` in `main/app_main.c`.
- `SHT ERROR`: swap/check SDA and SCL, confirm 3V3 and shared GND.
- Only one AB bit changes: COM is probably connected to the wrong encoder pad.
- Left/right reversed: swap encoder A and B.
