# CoPet Cloud API — MVP contract

This is the device-facing contract, not a commitment to any specific search,
weather, STT or AI provider.

## Transport constraints

- HTTPS with server certificate verification.
- One active request per device.
- Request body normally <= 512 bytes.
- Response body <= 2048 bytes.
- Client timeout: 10 seconds for text, 30 seconds for audio.
- UTF-8 JSON for text endpoints.
- `request_id` is copied from request to response.
- Third-party provider keys exist only on the backend.

## POST `/v1/query`

Used first for preset queries, weather and short typed/development queries.

Request:

```json
{
  "request_id": "42",
  "type": "weather",
  "text": "Какая погода сегодня?",
  "locale": "ru-RU",
  "timezone": "Europe/Samara"
}
```

Success response:

```json
{
  "request_id": "42",
  "status": "ok",
  "text": "Сегодня около 18 градусов, возможен дождь.",
  "mood": "helpful",
  "ttl_sec": 300
}
```

Error response:

```json
{
  "request_id": "42",
  "status": "error",
  "error": "provider_timeout",
  "text": "Сервис пока не отвечает. Попробуй позже."
}
```

The device displays `text` and does not need to understand provider-specific
payloads.

## POST `/v1/audio-query`, later milestone

The ESP32 streams mono PCM from INMP441 instead of buffering the entire
recording in RAM.

```text
Content-Type: application/octet-stream
X-Audio-Format: pcm_s16le
X-Sample-Rate: 16000
X-Channels: 1
X-Locale: ru-RU
```

The recording is explicitly started by the user and limited to a short fixed
duration. The response uses the same JSON shape as `/v1/query`.

## Device behavior

| Condition | Device action |
|---|---|
| no Wi-Fi | show offline card; keep local mode usable |
| HTTP 4xx | show configuration/auth error; do not retry continuously |
| HTTP 5xx/timeout | show temporary error; bounded retry with backoff |
| response > 2 KB | reject as protocol error |
| invalid JSON | reject; log request ID and error |

## Authentication

For local development the gateway can be limited to the developer's network.
Before any public deployment, use a revocable per-device token stored in NVS.
Never embed a search/AI provider master key in firmware.
