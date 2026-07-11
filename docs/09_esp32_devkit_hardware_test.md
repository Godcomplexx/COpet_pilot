# ESP32 DevKit hardware test

This test firmware is for the **ESP32-WROOM-32 DevKit shown in the photos**.
It is not the ESP32-S2 Saola or the planned ESP32-S3 board.

The current test proves seven behaviors:

1. ST7789 displays a menu and mode screens.
2. SHT3x reports room temperature and humidity.
3. The three-contact roller encoder changes the menu selection.
4. TTP223 opens a mode with a short touch and returns with a long touch.
5. Focus Mode runs a non-blocking 25/5 work and break timer.
6. Phone Bridge advertises over BLE only while its mode is open.
7. Audio Loopback routes INMP441 microphone samples to MAX98357A.

## Wiring

Disconnect USB power before changing wires.

### ST7789 240x240

| Display | ESP32 DevKit |
|---|---:|
| VCC | 3V3 |
| GND | GND |
| SCK | GPIO18 / D18 |
| SDA (MOSI) | GPIO5 / D5 |
| DC | GPIO16 / D16 |
| RES | GPIO17 / D17 |
| BLK | 3V3 |

This display module has no CS pin.
The GMT130-V1.0 uses an ST7789 controller with a 240x240 visible area. The
hardware test applies the module manufacturer's power, porch and gamma
initialization sequence and uses SPI mode 3, matching the clock waveform in the
manufacturer’s bit-banged reference implementation.

The UI is stored in a 240x240 RGB332 framebuffer and converted to RGB565 in
16-row DMA stripes. This keeps the display compatible with the ESP32's limited
contiguous RAM after NimBLE is enabled.

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

### TTP223 touch button

| TTP223 | ESP32 DevKit |
|---|---:|
| VCC | 3V3 |
| GND | GND |
| I/O | GPIO13 / D13 |

### MAX98357A and INMP441

| ESP32 DevKit | MAX98357A | INMP441 |
|---:|---|---|
| 5V/VIN | VIN | — |
| 3V3 | SD | VDD |
| GND | GND | GND + L/R |
| GPIO26 | BCLK | SCK |
| GPIO25 | LRC | WS |
| GPIO27 | DIN | — |
| GPIO34 | — | SD |

Leave MAX98357A `GAIN` disconnected. Connect a speaker only across `SPK+` and
`SPK-`; neither output is ground. Test one speaker first. Two speakers of
unknown impedance should be connected in series, not parallel.

Keep the microphone away from the speaker during loopback testing to reduce
acoustic feedback.

### Deferred hardware

MPU6050 is deferred.

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

After the color test the screen shows:

```text
COPET MENU
> DESK MODE
  FOCUS MODE
  PHONE BRIDGE
  AUDIO LOOPBACK
  INPUT TEST
```

Turning the wheel changes the selected menu item by exactly one position per
physical detent. A short touch opens the
selected screen. `DESK MODE` displays the SHT31 measurements. `INPUT TEST`
displays encoder and touch state. Holding touch for one second returns to the
menu. `FOCUS MODE` starts at 25:00; short touch starts or pauses it. After a
work period it prepares a 5:00 break. The serial monitor reports transitions.
`PHONE BRIDGE` advertises as `CoPet Pilot`; its screen changes from
`ADVERTISING` to `CONNECTED` when a phone connects through a BLE scanner.
Service `FFF0` contains writable characteristic `FFF1`. Writing ASCII text such
as `HELLO COPET` displays it on the screen.
`AUDIO LOOPBACK` starts I2S only while the mode is open. Speaking into the
microphone changes the level bar and should be heard through the speaker.
Before loopback it plays a two-second 1 kHz output test tone.

## Common failures

- White screen: check BLK=3V3, DC=GPIO16, RES=GPIO17, SCK=GPIO18 and
  SDA=GPIO5.
- Incorrect colors: change `esp_lcd_panel_invert_color(s_panel, true)` to
  `false`.
- Shifted image: adjust `LCD_X_GAP` or `LCD_Y_GAP` in `main/app_main.c`.
- `SHT ERROR`: swap/check SDA and SCL, confirm 3V3 and shared GND.
- Only one AB bit changes: COM is probably connected to the wrong encoder pad.
- Left/right reversed: swap encoder A and B.
- Count jumps several values per detent: verify the serial log contains one
  `ENC STEP` line per physical click.
- Alternating forward/backward steps while rotating one way: use the measured
  three-state cycle `10 -> 11 -> 01 -> 10`.
- Phone cannot find CoPet: open Phone Bridge first and scan with a BLE scanner,
  not only the phone's normal Bluetooth settings.
- Text does not appear: write ASCII/UTF-8 text to characteristic `FFF1`, not to
  the service UUID `FFF0`; maximum length is 64 bytes.
- Audio level stays at zero: check INMP441 VDD=3V3, L/R=GND, SD=GPIO34, and
  shared SCK/WS.
- Level moves but the speaker is silent: check MAX98357A SD=3V3, VIN=5V,
  DIN=GPIO27, and the speaker across SPK+/SPK-.
- Repeated reboot with `LCD framebuffer allocation failed`: an old RGB565
  framebuffer build is still flashed; rebuild and flash the RGB332 stripe
  version.
