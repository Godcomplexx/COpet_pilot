# 08. Project Milestones

## M0 — Planning

Done when:
- components listed;
- interfaces mapped;
- pin table drafted;
- learning log created.

## M1 — First boot

Done when:
- ESP32-WROOM-32 boots;
- logs show state machine state;
- project has component structure.

## M2 — Face and menu

Done when:
- ST7789 shows CoPet face;
- encoder scrolls menu;
- touch starts Focus Mode.

## M3 — Desk sensors

Done when:
- SHT31 readings shown;
- MPU6050 detects movement/tilt;
- Desk Mode reacts to environment.

## M4 — Sound

Done when:
- menu beep works;
- focus complete sound plays;
- volume/power behavior acceptable.

## M5 — Firmware architecture

Done when:
- boot opens Desk Mode;
- encoder opens Menu;
- display/input/sensors/modes are separated from app_main;
- state transitions have host tests.

Status (done): boot opens Desk and the encoder opens Menu. Mode logic is in
`main/modes/` (`desk`, `menu`, `focus`, `animation`) and per-screen rendering in
`main/ui/` on a shared `ui_canvas`. The hardware now lives behind driver modules
in `main/drivers/` — `copet_display` (ST7789/SPI + framebuffer), `copet_i2c`
(shared bus) + `copet_sht31`, `copet_encoder`, plus `mpu6050`, `touch_button`
and `copet_audio`. `app_main.c` is a thin layer that only runs the boot sequence
and the main loop, wiring inputs to modes and mode state to renderers. State
transitions for focus/menu/animation are covered by host tests in `test/host/`
(run `test/host/run_tests.ps1`). The `assistant` module lands with M7.

## M6 — Direct Wi-Fi

Done when:
- credentials are provisioned and saved;
- reconnect does not block UI;
- SNTP or weather HTTPS request succeeds;
- offline behavior remains usable.

## M7 — Online Assistant

Done when:
- preset text query reaches CoPet Cloud API;
- compact result card is displayed;
- errors/timeouts are visible and recoverable;
- short INMP441 upload works after the text path is stable.

## M8 — Mini TV

Done when:
- SD card files are listed;
- one clip or image sequence plays;
- sound plays without blocking UI.

## Later — Outdoor Pilot

Done when:
- GPS fix detected;
- speed is displayed;
- no-fix state handled;
- distance/max speed logged.
