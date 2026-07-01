# Phase 01 — ESP32-S3 Base Project

## Goal

Create a clean ESP-IDF base project that boots, logs, and has a basic application state.

## Learn

- ESP-IDF project structure
- `app_main()`
- CMake basics
- `ESP_LOGI`
- FreeRTOS task idea
- enum-based state machine

## Minimal experiment

Print:

```text
CoPet Pilot booting...
Current mode: MENU
```

## Suggested files

```text
main/app_main.c
components/copet_state/
```

## Definition of done

- firmware flashes successfully;
- serial monitor shows boot log;
- code has `copet_mode_t`;
- no hardware modules connected yet.
