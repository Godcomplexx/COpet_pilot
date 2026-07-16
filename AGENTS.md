# AGENTS.md — CoPet Pilot

This file defines how an AI coding agent should help with the **CoPet Pilot** project.

The goal is **learning-first embedded development**, not copy-paste assembly. The agent must help the developer understand every module before integrating it.

---

## 1. Project summary

**CoPet Pilot** is a desktop-first embedded robot companion based on an
**ESP32-WROOM-32 DevKit**.

The device has:

- ST7789 240×240 TFT display for face, menu, animations, and Mini TV mode
- capacitive touch button for quick actions
- rotary encoder / mouse wheel for menu navigation
- SHT31 temperature/humidity sensor for Desk Mode
- MPU6050 IMU for motion, tilt, and wake-up behavior
- speaker through an amplifier for sounds
- SD card for local media, sounds, images, and logs
- direct Wi-Fi/HTTPS for time, weather, search, and cloud assistant functions
- INMP441 microphone for short cloud voice requests after text HTTPS works
- optional diagnostic BLE, not a required phone bridge
- GPS/GNSS module + antenna in a later product version

Core idea:

```text
ESP32-WROOM-32 = body, display, sensors, audio, local state machine
CoPet Cloud API = STT, search, AI, provider secrets, compact responses
Wi-Fi/HTTPS = optional online layer; local behavior remains offline-first
```

---

## 2. Main rule

Do not build a giant final solution at once.

Always work module by module:

```text
ESP32-WROOM-32 boot directly into Desk Mode
→ ST7789 display
→ menu UI
→ encoder + touch input
→ SHT31
→ MPU6050
→ audio
→ SD card
→ firmware architecture refactor
→ direct Wi-Fi + one HTTPS request
→ Online Assistant
→ SD / Mini TV
→ GPS Outdoor Mode in a later version
```

Every module must have:

1. a small standalone test,
2. a short explanation of the hardware interface,
3. a visible result on serial monitor or screen,
4. a note in `docs/05_learning_log_template.md` or a new learning log entry.

---

## 3. Learning-first behavior for the agent

The agent should not simply paste large blocks of finished firmware.

For every task, the agent should answer in this order:

1. **What we are trying to prove**
2. **What hardware interface is involved**
3. **Minimal wiring / pin assumptions**
4. **Smallest test program or patch**
5. **How to verify it worked**
6. **Common failure cases**
7. **What the user learned**

When code is needed, prefer small patches and explain the important lines.

Bad behavior:

```text
Here is a complete project, just copy it.
```

Good behavior:

```text
First we will prove that SPI display works.
This test only initializes ST7789 and draws three primitives.
After that we can integrate it into the menu system.
```

---

## 4. Technology constraints

Use:

- ESP-IDF
- C or C++ suitable for embedded firmware
- FreeRTOS tasks only when they solve a real concurrency problem
- CMake project structure
- modular components

Do not use:

- Arduino framework as the main implementation
- random full-project code from the internet
- blocking delays everywhere
- hidden magic libraries without explaining what they do
- mandatory phone application or BLE bridge for normal operation
- third-party API keys in firmware or Git
- HTML scraping on the ESP32; use a compact JSON API

---

## 5. Hardware inventory

| Component | Use | Interface |
|---|---|---|
| ESP32-WROOM-32 DevKit | main controller | core |
| ST7789 240×240 TFT | face/menu/Mini TV | SPI |
| Touch button | quick action / Focus | GPIO or capacitive touch |
| Rotary encoder | menu scrolling | GPIO A/B + switch |
| SHT31 | temperature/humidity | I2C |
| MPU6050 | motion/tilt/wake-up | I2C |
| Speaker | sounds/audio | amplifier required |
| MAX98357A / PAM amp | speaker driver | I2S or analog/PWM |
| SD card | media/logs | SPI |
| Wi-Fi | direct cloud connection | 2.4 GHz + HTTPS |
| BLE diagnostic | optional hardware test | BLE GATT |
| GPS/GNSS module | next-version Outdoor Mode | UART |
| INMP441 microphone | short cloud voice input | I2S |

Important note:

```text
GPS antenna alone is not enough.
A GNSS receiver module is required.
```

---

## 6. Firmware architecture

Preferred structure:

```text
firmware/
  main/
    app_main.c

  components/
    copet_app/
      app_state.c
      app_state.h

    copet_ui/
      ui.c
      ui.h
      menu.c
      menu.h
      animations.c
      animations.h

    copet_input/
      encoder.c
      encoder.h
      touch_button.c
      touch_button.h

    copet_sensors/
      sht31.c
      sht31.h
      mpu6050.c
      mpu6050.h

    copet_audio/
      audio.c
      audio.h

    copet_storage/
      sd_card.c
      sd_card.h

    copet_network/
      wifi_service.c
      wifi_service.h
      http_service.c
      http_service.h

    copet_assistant/
      assistant_service.c
      assistant_service.h
      assistant_protocol.c
      assistant_protocol.h

    copet_gps/             # later product version
      gps_parser.c
      gps_parser.h
```

Use a state machine instead of random flags.

Main modes:

```c
typedef enum {
    COPET_MODE_BOOT,
    COPET_MODE_MENU,
    COPET_MODE_DESK,
    COPET_MODE_FOCUS,
    COPET_MODE_ASSISTANT,
    COPET_MODE_MINI_TV,
    COPET_MODE_SETTINGS,
    COPET_MODE_SLEEP
} copet_mode_t;
```

Input events:

```c
typedef enum {
    INPUT_NONE,
    INPUT_ENCODER_LEFT,
    INPUT_ENCODER_RIGHT,
    INPUT_ENCODER_PRESS,
    INPUT_TOUCH_SHORT,
    INPUT_TOUCH_LONG,
    INPUT_TOUCH_DOUBLE
} input_event_t;
```

---

## 7. Mode definitions

### Desk Mode

Autonomous table mode.

Features:

- face animation
- time/status display
- room comfort from SHT31
- idle/sleep reactions
- movement reactions from MPU6050

### Focus Mode

Study/work mode.

Features:

- 25/5 timer first
- start/pause/finish controls
- sound on start/end
- visual focus animation
- local focus session counter

### Assistant Mode

Manual online mode. The device remains usable when offline.

Features:

- encoder/touch selects a preset query or starts a short recording
- ESP32 sends one bounded HTTPS request to CoPet Cloud API
- cloud performs STT/search/AI and returns compact JSON
- ESP32 shows short UI cards and animations

Do not store provider secrets on the device. Do not parse search HTML.

### Mini TV Mode

Local media mode.

Preferred source:

```text
SD card → ESP32-WROOM-32 → ST7789 + speaker
```

Use local clips, not YouTube streaming.

Recommended formats:

```text
MJPEG 240×240 10–15 fps
WAV or simple decoded audio
```

### Outdoor Mode, deferred

Later product/hardware revision, not part of the current MVP or menu.

Features:

- GPS speed
- GPS fix status
- distance estimate
- motion state from MPU6050
- backpack/bicycle animation

Speed should come from GPS/GNSS, not from integrating accelerometer data.

---

## 8. Development phases

### Phase 1 — ESP32-WROOM-32 base

Goal:

```text
Serial monitor prints boot log and current app mode.
```

Learn:

- ESP-IDF project structure
- CMake
- `app_main()`
- logging

Done when:

```text
CoPet Pilot booting...
Mode: MENU
```

appears in monitor.

---

### Phase 2 — ST7789 display

Goal:

```text
Display shows color fill, text, and a simple CoPet face.
```

Learn:

- SPI
- display initialization
- DC/RST/BLK pins
- pixel coordinates

Done when:

```text
CoPet Boot OK
:)
```

is visible on screen.

---

### Phase 3 — Encoder + touch button

Goal:

```text
User can navigate the on-device menu.
```

Learn:

- GPIO
- debounce
- encoder quadrature
- event-driven input

Done when:

```text
encoder rotation changes menu selection
encoder press opens a mode
touch long press returns/back or starts focus
```

---

### Phase 4 — SHT31 + MPU6050

Goal:

```text
Device reads environment and movement data.
```

Learn:

- I2C
- device addresses
- register reads/writes
- raw sensor conversion
- thresholds and filtering

Done when screen can show:

```text
Temp: 24.1 C
Humidity: 46 %
Motion: stable / moved / tilted
```

---

### Phase 5 — Audio

Goal:

```text
Device plays UI sounds.
```

Learn:

- speaker amplifier requirement
- PWM or I2S basics
- WAV playback later

Done when:

```text
menu select beep works
focus complete sound works
```

---

### Phase 6 — Firmware architecture refactor

Goal:

```text
Working hardware behavior is split out of app_main without regressions.
```

Learn:

- typed application events
- Desk-first state machine
- driver/service/mode/UI boundaries
- bounded queues
- host tests for pure state logic

Done when:

```text
boot opens Desk Mode
encoder opens Menu
long touch returns Home
app_main only wires modules together
```

---

### Phase 7 — Direct Wi-Fi

Goal:

```text
CoPet connects directly to Wi-Fi without blocking local UI.
```

Learn:

- SoftAP provisioning
- NVS credentials
- reconnect state machine
- SNTP
- HTTPS and certificate verification

Done when:

```text
Desk Mode remains responsive while Wi-Fi reconnects
one HTTPS weather/time request returns a compact result card
```

---

### Phase 8 — Online Assistant

Goal:

```text
CoPet sends one bounded query to CoPet Cloud API.
```

Learn:

- compact JSON protocol
- timeouts and retry policy
- I2S audio streaming after text query works
- cloud STT/search/AI boundaries

Done when:

```text
preset text query displays a short answer
offline/timeout state returns safely to Desk Mode
```

---

### Phase 9 — SD card + Mini TV

Start only after the no-CS display and SD SPI topology is resolved.

Done when a file list, one image sequence, and bounded audio playback work
without blocking UI.

### Later version — GPS Outdoor Mode

Do not implement GPS, speed, distance, or outdoor states in the current MVP.

---

## 9. Coding standards

- Keep modules small.
- Use clear names: `copet_ui_init()`, `sht31_read()`, `gps_parse_nmea()`.
- Avoid global state unless it belongs to the app state machine.
- No long blocking loops in UI/audio/network code.
- Prefer explicit error handling.
- Log hardware failures clearly.
- Add comments only where they explain hardware behavior or non-obvious logic.

Example error style:

```c
ESP_LOGE(TAG, "SHT31 read failed: %s", esp_err_to_name(err));
```

---

## 10. Verification style

Every change should answer:

```text
What did we test?
What was expected?
What actually happened?
How do we know it works?
What should be tested next?
```

Do not mark a module done only because it compiles.

A module is done only when the hardware behavior is visible and repeatable.

---

## 11. README / documentation expectations

Keep documentation current.

At minimum update:

- `docs/02_hardware_map.md` when wiring changes
- `docs/07_pin_planning_template.md` when pin assignments change
- `docs/05_learning_log_template.md` or a learning log entry after each lesson
- `README.md` when project capabilities change

For portfolio value, include:

```text
- photos of wiring
- short video demos
- screenshots of serial monitor
- diagrams of architecture
- notes about bugs and fixes
```

---

## 12. What the agent should ask before coding

If missing, ask for:

1. whether the known ESP32-WROOM-32 DevKit wiring has changed
2. display pin labels and voltage
3. sensor module labels
4. current pin plan
5. ESP-IDF version
6. whether the user wants C or C++ for this module
7. whether the goal is learning explanation, minimal test, or integration patch

Do not ask unnecessary questions when a safe minimal test can be written with placeholders.

---

## 13. Current priority

The current recommended next step is:

```text
Phase 6: architecture refactor of the working hardware test
```

Then:

```text
Phase 7: direct Wi-Fi, SNTP, then one HTTPS request
```

Do not start GPS or Mini TV before the local architecture and direct Wi-Fi path are stable. Prove a text HTTPS request before cloud audio upload.

---

## 14. Final principle

This project should prove embedded engineering skills:

```text
SPI + I2C + GPIO + I2S + Wi-Fi + HTTPS
state machine design
small-screen UI
device behavior design
sensor-driven interaction
media from SD card
phone-assisted AI bridge
```

The best result is not just a working gadget.

The best result is a project where the developer can explain:

```text
why each module exists,
how it communicates,
what can fail,
how it was tested,
and how it integrates into the final product.
```
