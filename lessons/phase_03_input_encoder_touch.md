# Phase 03 — Encoder and Touch Button

## Goal

Control the menu with mouse wheel/encoder and capacitive touch.

## Learn

- GPIO input
- pull-up/pull-down
- rotary encoder A/B signals
- debouncing
- input events
- short/long press logic

## Minimal experiment

Serial monitor logs:

```text
ENCODER_RIGHT
ENCODER_LEFT
ENCODER_PRESS
TOUCH_SHORT
TOUCH_LONG
```

Then connect to menu:

```text
> Desk
  Focus
  Phone Bridge
  Mini TV
  Outdoor
  Settings
```

## Event model

```c
typedef enum {
    INPUT_ENCODER_LEFT,
    INPUT_ENCODER_RIGHT,
    INPUT_ENCODER_PRESS,
    INPUT_TOUCH_SHORT,
    INPUT_TOUCH_LONG
} input_event_t;
```

## Definition of done

- encoder scrolls menu reliably;
- touch short/long are different;
- no random double triggering.
