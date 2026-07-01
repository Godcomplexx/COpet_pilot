# Phase 08 — GPS Outdoor Mode

## Goal

Use GPS/GNSS module for outdoor speed and distance.

## Learn

- UART
- NMEA sentences
- GPS fix/no fix
- speed over ground
- knots to km/h
- satellite count

## Minimal experiment

Print raw NMEA:

```text
$GNRMC,...
$GNGGA,...
```

Then parse:

```text
GPS: FIX
Speed: 12.4 km/h
Satellites: 9
```

## Important

MPU6050 is not reliable for real speed.  
Use GPS for speed; use MPU6050 for motion/wake-up/tilt.

## Definition of done

- raw NMEA visible;
- fix state detected;
- speed shown;
- no-fix state handled gracefully.
