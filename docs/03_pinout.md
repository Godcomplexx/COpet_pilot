# 03. Pinout

Wiring reference for the ESP32-WROOM-32 DevKit prototype. Every number here is
taken from the driver sources, which stay the single source of truth — the pins
are compile-time constants, not Kconfig options.

## Reading the silkscreen

On ESP32 DevKit boards the `D<n>` label on the silkscreen **is** `GPIO<n>`:
`D18` is GPIO18, `D5` is GPIO5. This differs from ESP8266 / WeMos D1 mini,
where `D1` is a board-specific alias for GPIO5. On an ESP32 DevKit V1, `D1` is
literally GPIO1 = `TX0`, which belongs to the USB serial console and must stay
unused. Some boards print a function name instead of the number — those aliases
are given in the tables below.

## Display — ST7735 1.77-inch 128x160 module, SPI

Bus `SPI2_HOST`, 1 MHz pixel clock, SPI mode 0. Panel offsets X=0, Y=0.
The 240x240 UI is scaled to a centered 128x128 image, with 16 black rows
above and below to preserve its proportions.

| Module pin | ESP32 GPIO | DevKit label | Note |
|---|---|---|---|
| 1 — `GND` | GND | `GND` | Common ground |
| 2 — `VCC` | 3V3 | `3V3` | Module power |
| 3 — `SCK` | **GPIO18** | `D18` | SPI clock |
| 4 — `SDA` / `MOSI` | **GPIO5** | `D5` | SPI data, not I2C; strapping pin |
| 5 — `RES` / `RST` | **GPIO17** | `D17` / `TX2` | Hardware reset |
| 6 — `RS` / `DC` | **GPIO16** | `D16` / `RX2` | Command/data select |
| 7 — `CS` | GND | `GND` | Permanently selected; physical connection required |
| 8 — `LEDA` | — | — | Backlight supply; actual supply/current-limiting circuit not yet documented, not a firmware GPIO |

Wire CS to GND: the driver uses `cs_gpio_num = -1`. MISO is unused
(`miso_io_num = -1`); this design writes to the panel without reading it back.
The display needs a dedicated SPI bus while CS is tied low. Sharing its bus
with microSD requires a separately controlled display CS and a driver change.
Follow the module's own printed pin numbers, not a different seller variant.
See [display replacement and validation](29_st7735_display.md).

Source: [main/drivers/copet_display.c:14-25](../main/drivers/copet_display.c#L14-L25)

## Sensors — shared I2C bus

Port `I2C_NUM_0`, internal pull-ups enabled in software.

| Bus line | ESP32 GPIO | DevKit label |
|---|---|---|
| `SDA` | **GPIO21** | `D21` |
| `SCL` | **GPIO22** | `D22` |

Both sensors hang off the same two wires plus 3V3 and GND:

| Sensor | Module pin | Goes to | I2C address |
|---|---|---|---|
| SHT31 (temp/humidity) | `SDA` | GPIO21 | `0x44` default, `0x45` if `ADDR` is pulled high |
| | `SCL` | GPIO22 | |
| | `VIN` / `GND` | 3V3 / GND | |
| MPU6050 / MPU6500 (IMU) | `SDA` | GPIO21 | `0x68` with `AD0` low, `0x69` with `AD0` high |
| | `SCL` | GPIO22 | |
| | `VCC` / `GND` | 3V3 / GND | |
| | `AD0` | GND | selects `0x68` |

The driver probes both addresses for each device, so either strap works. The
IMU sold as MPU6050 reports `WHO_AM_I = 0x70` (MPU6500-compatible); the driver
accepts both identities.

Source: [main/drivers/copet_i2c.c:5-8](../main/drivers/copet_i2c.c#L5-L8),
[main/drivers/copet_sht31.c:7-8](../main/drivers/copet_sht31.c#L7-L8),
[main/drivers/mpu6050.c:10-11](../main/drivers/mpu6050.c#L10-L11)

## Rotary encoder (three-contact mouse wheel)

Internal pull-ups enabled, no interrupts — a task polls the pair.

| Encoder pin | ESP32 GPIO | DevKit label |
|---|---|---|
| `A` | **GPIO32** | `D32` |
| `B` | **GPIO33** | `D33` |
| `C` (common) | GND | `GND` |

The wheel has no push-button wired; its role is taken by the touch pad.

Source: [main/drivers/copet_encoder.c:9-13](../main/drivers/copet_encoder.c#L9-L13)

## Touch button — TTP223

Active high, internal pull-**down** enabled.

| Module pin | ESP32 GPIO | DevKit label |
|---|---|---|
| `I/O` / `OUT` | **GPIO13** | `D13` |
| `VCC` | 3V3 | `3V3` |
| `GND` | GND | `GND` |

Source: [main/drivers/touch_button.c:12-18](../main/drivers/touch_button.c#L12-L18)

## Audio — I2S, shared clocks

One I2S peripheral drives the amplifier and the microphone; `BCLK` and `WS` are
common to both, only the data lines differ.

| Signal | ESP32 GPIO | DevKit label | Amplifier (MAX98357A) | Mic (INMP441) |
|---|---|---|---|---|
| `BCLK` | **GPIO26** | `D26` | `BCLK` | `SCK` |
| `LRC` / `WS` | **GPIO25** | `D25` | `LRC` | `WS` |
| `DOUT` (ESP → amp) | **GPIO27** | `D27` | `DIN` | — |
| `DIN` (mic → ESP) | **GPIO34** | `D34` / `VDET1` | — | `SD` |

Extra straps on the modules:

- MAX98357A `SD` → 3V3 (enable), `GAIN` left floating for the default 9 dB.
- INMP441 `L/R` → GND, so the mic occupies the left slot.
- Both modules take 3V3 and GND.

The bus runs 32-bit stereo slots; clips are 16 kHz mono. TX stays enabled
permanently because the INMP441 is an I2S slave and needs the shared clock to
keep running.

Source: [main/drivers/copet_audio.c:14-17](../main/drivers/copet_audio.c#L14-L17),
[main/drivers/audio_loopback.c:15-18](../main/drivers/audio_loopback.c#L15-L18)

## Full map, sorted by GPIO

| GPIO | DevKit label | Peripheral | Function |
|---|---|---|---|
| 5 | `D5` | ST7735 | `MOSI` / `SDA` |
| 13 | `D13` | TTP223 | touch output |
| 16 | `D16` / `RX2` | ST7735 | `DC` |
| 17 | `D17` / `TX2` | ST7735 | `RST` |
| 18 | `D18` | ST7735 | `SCLK` |
| 21 | `D21` | SHT31 + MPU6050 | I2C `SDA` |
| 22 | `D22` | SHT31 + MPU6050 | I2C `SCL` |
| 25 | `D25` | MAX98357A + INMP441 | I2S `LRC` / `WS` |
| 26 | `D26` | MAX98357A + INMP441 | I2S `BCLK` |
| 27 | `D27` | MAX98357A | I2S `DOUT` |
| 32 | `D32` | encoder | channel `A` |
| 33 | `D33` | encoder | channel `B` |
| 34 | `D34` | INMP441 | I2S `DIN` |

Thirteen GPIOs in use. Nothing is double-booked except the two I2S clocks and
the I2C pair, which are buses by design.

## Notes and hazards

- **GPIO34 is input-only.** It has no output driver and no internal pull
  resistors, which is exactly right for the microphone data line and wrong for
  anything else.
- **GPIO5 is a strapping pin.** It is sampled at boot and must not be held low
  externally at reset. As an SPI output driven by the ESP32 it is fine, but do
  not add a pull-down on this line.
- **Do not use GPIO0, 2, 12, 15** for new peripherals — they are strapping pins
  that change boot behaviour. GPIO12 held high at reset can brick the boot
  voltage selection.
- **GPIO1 and GPIO3** (`TX0` / `RX0`) belong to the USB serial console. On
  DevKit V1 silkscreen these are the `D1` / `D3` pads; leave them alone.
- **GPIO6-11** are wired to the internal SPI flash and are not available.
- **Power the speaker through the amplifier**, never straight off a GPIO.
- The display is SPI, not I2C.
- The user reports microSD is already integrated, but its GPIO assignments
  have not been found in this working copy. Do not infer them from common
  ESP32 examples; confirm against the SD implementation before PCB layout.
- GPS/Outdoor Mode is out of scope
  ([adr-004](architecture/adr-004-outdoor-mode-out-of-scope.md)).

## Changing a pin

Pins are constants in the `enum` at the top of each driver, not Kconfig
options. To move one, edit the driver and rebuild — and update this file, since
nothing enforces the two staying in sync.
