# I2S audio loopback learning log

Date: 2026-07-03

## What we are trying to prove

Prove the complete local audio path:

```text
voice → INMP441 → ESP32 I2S RX → software gain
      → ESP32 I2S TX → MAX98357A → speaker
```

This test does not recognize speech or store audio.

## Interface and pins

Both devices share clocks:

| Signal | ESP32 | INMP441 | MAX98357A |
|---|---:|---|---|
| Bit clock | GPIO26 | SCK | BCLK |
| Word select | GPIO25 | WS | LRC |
| Microphone data | GPIO34 | SD | — |
| Speaker data | GPIO27 | — | DIN |

INMP441 uses 3V3. MAX98357A uses 5V/VIN and `SD` is tied to 3V3.

## Smallest test

- 16 kHz standard Philips I2S
- 32-bit stereo slots
- INMP441 `L/R=GND`, therefore read the left slot
- software gain x32 with clipping
- three-second 1 kHz high-level output tone before microphone loopback
- copy the microphone sample to both MAX98357A input slots
- update a 0–100 microphone level bar five times per second

## Verification

1. Open `AUDIO LOOPBACK`.
2. Confirm `OUTPUT TEST` and hear a three-second 1 kHz tone.
3. Confirm `LOOPBACK ON`.
4. Speak near the microphone; confirm the level bar moves.
5. Confirm the voice is heard from one connected speaker.
6. Hold touch; confirm clocks and loopback stop before returning to menu.

Expected log:

```text
Ready: TX=I2S0 master RX=I2S1 slave BCLK=26 WS=25 MIC_SD=34 AMP_DIN=27
OUTPUT TEST: 1000 Hz for 3000 ms
DIN SELF-TEST PASS: GPIO27 samples=...
OUTPUT TEST finished
Loopback running: mic D34 -> speaker D27
Loopback stopped
```

## Common failures

- Loud squeal: acoustic feedback; separate microphone and speaker.
- Constant maximum level: wrong I2S channel or data alignment.
- Level moves but no sound: amplifier shutdown or speaker wiring.
- No level: microphone power, L/R selection, or SD wiring.

## What was learned

Full-duplex I2S shares clock and word-select signals while RX and TX use
separate data lines. The audio task owns I2S reads and writes; UI only reads a
small level value.

For the output fault diagnostic, speaker TX was moved to I2S0 in master mode
and microphone RX to I2S1 in slave mode. Both controllers use the same physical
BCLK and WS wires. This keeps MAX98357A output timing independent from the
microphone DMA channel while preserving the loopback feature.

During the three-second output tone, I2S1 temporarily reads GPIO27 instead of
the microphone. Seeing both positive and negative full-scale samples produces
`DIN SELF-TEST PASS`, proving that ESP32 drives the configured MAX98357A data
pin. The RX input is then reconfigured to microphone GPIO34.

The output diagnostic plays two distinguishable formats:

1. 1 kHz in Philips I2S format for MAX98357A.
2. 1.5 kHz in left-justified format for MAX98357B.

After both tests, TX and RX return to Philips format because INMP441 uses
standard I2S.

## Loud output diagnostic

The standalone output test now sends a 1 kHz square wave for three seconds at
75% of the signed 32-bit digital range. Microphone loopback uses 32x software
gain with saturation, so clipping is expected when speaking very close to the
microphone. This diagnostic separates I2S output/amplifier faults from a quiet
microphone signal.
