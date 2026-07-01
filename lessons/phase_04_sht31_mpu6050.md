# Phase 04 — SHT31 and MPU6050

## Goal

Read room comfort data and motion data through I2C.

## Learn

- I2C bus
- device address
- sensor commands
- register reads/writes
- raw data conversion
- motion thresholds

## Minimal experiments

### SHT31

Show:

```text
Temperature: 23.8 C
Humidity: 45 %
```

### MPU6050

Show:

```text
ax ay az
state: stable / moved / tilted
```

## CoPet behavior

| Condition | Reaction |
|---|---|
| hot | tired face |
| cold | shivering face |
| comfortable | happy face |
| tilted | surprised face |
| no movement | sleepy face |

## Definition of done

- I2C scanner finds both modules;
- SHT31 values update;
- MPU6050 movement state works;
- Desk Mode reacts to sensor data.
