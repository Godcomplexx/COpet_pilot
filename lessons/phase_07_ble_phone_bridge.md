# Phase 07 — BLE Phone Bridge

## Goal

Make ESP32-S3 exchange small structured messages with phone.

## Learn

- BLE advertising
- GATT service
- characteristics
- write/notify
- JSON protocol
- connection state

## First protocol

Phone → CoPet:

```json
{
  "type": "weather_update",
  "temp": 18,
  "condition": "rain",
  "text": "Take umbrella"
}
```

CoPet → Phone:

```json
{
  "type": "device_event",
  "event": "focus_started"
}
```

## Do not send video over BLE

BLE is for:
- weather;
- text;
- settings;
- AI answer;
- focus stats;
- control messages.

## Definition of done

- phone connects;
- ESP32 shows connected state;
- phone sends weather JSON;
- CoPet displays weather.
