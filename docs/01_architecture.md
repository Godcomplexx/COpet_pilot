# 01. CoPet Pilot Architecture

## Product boundary

CoPet Pilot is an offline-first desktop companion built on the
ESP32-WROOM-32 DevKit. Local behavior never depends on a phone or cloud
connection. Wi-Fi adds time, weather, search and assistant responses, but a
network failure must not block the face, menu, sensors, Focus Mode or local
animations.

## System context

```text
                           Internet
                              │ HTTPS
                              ▼
                    ┌───────────────────┐
                    │  CoPet Cloud API  │
                    │ STT/search/AI/API │
                    └─────────┬─────────┘
                              │ compact JSON
                              ▼
┌──────────────────────────────────────────────────────────┐
│                 ESP32-WROOM-32 DevKit                    │
│                                                          │
│  local app/state ── UI ── ST7789                         │
│        │                                                 │
│        ├── input ── encoder + TTP223                     │
│        ├── sensors ── SHT31 + MPU6050                    │
│        ├── audio ── INMP441 + MAX98357A                  │
│        ├── storage ── NVS (+ SD later)                   │
│        └── network ── Wi-Fi + HTTPS                      │
└──────────────────────────────────────────────────────────┘

Optional browser
  └── opens the local CoPet-Setup page for Wi-Fi provisioning
```

The CoPet Cloud API is one small adapter backend. It hides provider secrets,
limits response size and converts external services to one device-friendly
contract. The ESP32 does not scrape search-result HTML and does not store
third-party API keys.

## Firmware boundaries

```text
app_main
  └── core: state + events
        ├── modes: Desk, Menu, Focus, Assistant, Animation, Settings
        ├── services: sensors, focus, Wi-Fi, HTTP, assistant, time
        ├── UI: framebuffer, primitives, screens
        └── drivers: SPI, I2C, I2S, GPIO, NVS
```

| Layer | Owns | Communicates through |
|---|---|---|
| drivers | hardware access | small C APIs and `esp_err_t` |
| services | reusable behavior and asynchronous work | typed events/queues |
| modes | mode rules and local state | service APIs and UI models |
| UI | framebuffer and screen rendering | immutable render data |
| core | transitions and event routing | `app_event_t` |

Drivers never select screens. Services never draw directly. Modes never
manipulate peripheral registers. `app_main.c` wires the modules together and
contains no feature implementation.

## Runtime model

The MVP uses a small number of tasks:

| Task | Responsibility |
|---|---|
| app/UI | consume events, update mode, render changed screen |
| network | reconnect Wi-Fi and execute one HTTPS request at a time |
| audio | own I2S RX/TX and bounded streaming buffers |
| sensor | low-rate SHT31/MPU6050 polling |

Queues are bounded. HTTP callbacks post a result event; the UI task never
waits for a network call. On the ESP32-WROOM-32, only one heavy network request
is active at a time.

## Application state machine

Screen wireframes and detailed user cases are documented in
[`docs/ux/01_user_cases_and_screen_flow.md`](ux/01_user_cases_and_screen_flow.md).

```text
BOOT ──init complete──> DESK_MODE

DESK_MODE ──encoder──> MENU ──short touch──> selected mode
    ▲                     │
    └────long touch───────┘
    └────10 s timeout─────┘

FOCUS / ASSISTANT / ANIMATION / MINI_TV / SETTINGS
    └──long touch──> DESK_MODE

DESK_MODE ──hold 5 s──> SLEEP ──touch/motion──> DESK_MODE
```

Network state is orthogonal to the UI mode:

```text
NET_UNCONFIGURED → NET_CONNECTING → NET_ONLINE
                         │              │
                         └→ NET_ERROR ←─┘
                              │ retry
                              └→ NET_CONNECTING
```

This separation prevents combinations such as `FOCUS_WIFI_CONNECTING` or
`DESK_WIFI_ERROR`. Every screen can read the same small network-status model.

## Online request flow

```text
user action
  → assistant mode
  → optional short I2S recording
  → network queue
  → HTTPS request to CoPet Cloud
  → bounded JSON response
  → ASSISTANT_RESULT event
  → result card
```

The first network milestone should be SNTP or weather text. Voice upload is a
later milestone because it combines I2S, streaming HTTP, TLS and cloud STT.

## Deferred scope

- Outdoor Mode and GPS/GNSS are a later hardware/product version.
- SD/Mini TV starts only after the display-without-CS bus conflict is resolved.
- A phone application is not planned until a use case appears that a local
  setup page cannot solve.
- Local speech recognition and a local LLM are outside WROOM MVP constraints.

## Architecture decisions

See `docs/architecture/`:

- ADR-001: ESP32-WROOM-32 is the product target.
- ADR-002: offline-first device with direct Wi-Fi and a small cloud gateway.
- ADR-003: Desk-first navigation and orthogonal network state.
- ADR-004: Outdoor Mode is deferred.
- Device-facing API: `docs/architecture/cloud_api_contract.md`.

## Official platform references

- [ESP32 Wi-Fi overview](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi-driver/overview.html)
- [ESP-IDF HTTP/S client](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/esp_http_client.html)
- [ESP-IDF 5.5 Wi-Fi provisioning](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/provisioning/wifi_provisioning.html)
