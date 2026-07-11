# MPU6050 learning log

Date: 2026-07-02

Status: deferred until the module is available. The prepared driver is not
part of the active firmware build.

## What we are trying to prove

Prove that MPU6050 can share the I2C bus with SHT31 and provide repeatable
acceleration and gyroscope samples.

## Interface

I2C at 100 kHz:

| Signal | ESP32 DevKit | MPU6050 |
|---|---:|---|
| Power | 3V3 | VCC |
| Ground | GND | GND |
| Data | GPIO21 | SDA |
| Clock | GPIO22 | SCL |

## Smallest test

1. Check addresses `0x68` and `0x69`.
2. Read register `WHO_AM_I` (`0x75`).
3. Wake the sensor through `PWR_MGMT_1` (`0x6B`).
4. Read the 14-byte sample beginning at `ACCEL_XOUT_H` (`0x3B`).
5. Display acceleration and a simple movement classification.

## Verification

- Stationary and flat: approximately `AZ 1.00`, state `STABLE`.
- Tilt the board: AX or AY changes and state becomes `TILTED`.
- Move it quickly: gyroscope threshold changes state to `MOVED`.
- SHT31 must continue updating on the same bus.

## Common failures

- `MPU NOT FOUND`: power, SDA/SCL, shared ground, or wrong module.
- Address mismatch: AD0 changes the address between `0x68` and `0x69`.
- Noisy state: thresholds need filtering after raw communication is proven.

## What was learned

Multiple addressed devices can share SDA and SCL. Device identity must be
verified before interpreting register data.
