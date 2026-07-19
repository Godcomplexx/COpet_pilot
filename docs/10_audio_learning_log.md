# MAX98357A audio test

Date: 2026-07-18

## What we are trying to prove

Prove that ESP32 transmits an audible diagnostic tone to MAX98357A over I2S.

## Interface and pins

| Signal | ESP32 pin | MAX98357A |
|---|---:|---|
| Bit clock | GPIO26 | BCLK |
| Word select | GPIO25 | LRC |
| Audio data | GPIO27 | DIN |
| Power | 5 V | VIN |
| Ground | GND | GND |

The speaker connects only between the amplifier's `SPK+` and `SPK-` outputs.

## Test

- Standard Philips I2S
- Three automatic profiles: 16 kHz/16-bit, 44.1 kHz/16-bit, 48 kHz/32-bit
- Different three-second square-wave tone for each profile
- The same signal in left and right slots
- Serial log reports tone start, write errors, and tone completion

## Verification

Expected log:

```text
MAX98357A diagnostic ready: BCLK=26 LRC=25 DIN=27
TEST 16 kHz / 16-bit Philips: 500 Hz for 3000 ms
TEST finished
TEST 44.1 kHz / 16-bit Philips: 1000 Hz for 3000 ms
TEST finished
TEST 48 kHz / 32-bit Philips: 1500 Hz for 3000 ms
TEST finished
```

The module is not complete until the tone is physically audible and repeatable.

Actual result on 2026-07-18: the diagnostic tones were physically audible
through the connected amplifier and speaker. The I2S transmit path and speaker
wiring are verified.

## Common failures

- `SD_MODE` is low and the amplifier is shut down.
- Amplifier VIN or GND is not connected.
- Speaker is connected to ground instead of across `SPK+` and `SPK-`.
- Two series speakers make the load impedance high and the sound quiet.
- A wire or solder joint is open.

## What was learned

I2S logs can prove that samples were accepted by the ESP32 peripheral, but the
one-way bus cannot prove that the amplifier received them or drove the speaker.
