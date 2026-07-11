# Menu and state machine learning log

Date: 2026-07-02

## What we are trying to prove

The encoder and touch button can control a small event-driven UI without audio
or microphone hardware.

## Hardware interfaces

- ST7789: SPI
- Encoder: two GPIO inputs with pull-ups
- TTP223: active-high GPIO input
- SHT31: I2C

## Minimal state machine

```text
MENU --short touch--> DESK MODE
MENU --short touch--> FOCUS MODE
MENU --short touch--> PHONE BRIDGE
MENU --short touch--> INPUT TEST
DESK/FOCUS/PHONE/INPUT --long touch--> MENU
```

The encoder changes the selected item only while the application is in `MENU`.

## Verification

1. Turn the wheel and confirm the highlight moves.
2. Select `DESK MODE` and touch briefly.
3. Confirm temperature and humidity update every two seconds.
4. Hold touch for one second and confirm the menu returns.
5. Open `INPUT TEST` and confirm the encoder count and `AB` bits change.
6. Open `FOCUS MODE`, start it, and confirm the timer decrements.
7. Open `PHONE BRIDGE` and confirm the phone sees `CoPet Pilot`.

## Common failures

- This mouse wheel uses `10 -> 11 -> 01 -> 10`, not the usual four-state
  quadrature cycle. Each state must remain stable for 12 ms before one
  `ENC STEP` is published.
- Reversed direction: swap encoder A and B.
- Touch opens immediately without contact: check that TTP223 is active high.
- `SHT ERROR`: check GPIO21/GPIO22 and the shared ground.

## What was learned

Input events change application state. Rendering depends on the current state;
individual drivers do not decide which screen should be shown.
