# Focus Mode learning log

Date: 2026-07-02

## What we are trying to prove

Run a 25-minute work timer and 5-minute break timer without blocking input,
sensor reads, or screen updates.

## Interface

No new hardware is required:

- TTP223 short touch: start or pause.
- TTP223 long touch: return to menu.
- ST7789: remaining time and timer state.

## State transitions

```text
WORK READY --touch--> WORK RUN --touch--> WORK PAUSE
WORK PAUSE --touch--> WORK RUN
WORK complete --> BREAK READY
BREAK READY --touch--> BREAK RUN
BREAK complete --> WORK READY
```

Completing a work period increments the local session counter.

## Verification

1. Open `FOCUS MODE`; screen shows `WORK READY` and `25:00`.
2. Touch briefly; state changes to `WORK RUN`.
3. Confirm the display reaches `24:59`.
4. Touch briefly; confirm the timer stops at the current value.
5. Touch again; confirm the countdown resumes.
6. Hold touch for one second; confirm the menu returns.

## Common failures

- Timer skips while the display updates: elapsed time must come from
  `esp_timer_get_time()`, not loop counts.
- Long touch also toggles pause: the touch driver must emit only one event.
- Returning to menu loses state: timer state must belong to the application,
  not the renderer.

## What was learned

A non-blocking timer stores state and compares monotonic timestamps. It does
not wait in a delay loop, so other device functions remain responsive.
