# Product sound event plan

Date: 2026-07-19

## What we are trying to prove

Prove that user-selected sound assets can be played from embedded flash without
blocking the UI, and that `SOUND OFF` controls all product sounds.

## MVP sound events

| Embedded asset | Source MP3 | Trigger | Duration |
|---|---|---:|---|
| `menu_confirm.pcm` | `matthewvakaliuk73627-mouse-click-290204.mp3` | opening the selected mode | 170 ms |
| `menu_move.pcm` | `universfield-new-notification-040-493469.mp3` | each visible menu selection change | 210 ms |
| `menu_move.pcm` | `universfield-new-notification-040-493469.mp3` | switching Desk data between ROOM and OUT | 210 ms |
| `focus_start.pcm` | `universfield-notification-beep-229154.mp3` | starting or resuming work or break | 1.40 s |
| `menu_confirm.pcm` | `matthewvakaliuk73627-mouse-click-290204.mp3` | pausing work or break | 170 ms |
| `focus_complete.pcm` | `universfield-positive-notification-351299.mp3` | work or break reaches zero | 590 ms |

No sound is attached to sensor updates or MPU6050 movement events.

## Conversion and playback format

- mono
- signed 16-bit samples
- 16 kHz sample rate
- raw little-endian PCM embedded in the application image
- short silence trimmed from the beginning and end
- 25% software output gain in the driver

Run `tools/convert_sounds.ps1` to reproduce the PCM assets from the source
MP3 files. MP3 decoding is intentionally not included in the ESP32 firmware.

## Settings behavior

The Settings screen exposes `SOUND ON/OFF`. A short touch toggles it; a long
touch returns to Desk Mode. The current value is runtime-only and returns to
`ON` after reboot. NVS persistence is a separate follow-up lesson.

## Verification

1. Rotate the encoder: each visible category change plays `menu_move.pcm`.
2. Short-touch Desk Mode: changing `ROOM` to `OUT` or back plays
   `menu_move.pcm` once.
3. Short-touch a menu item: `menu_confirm.pcm` plays once.
4. Start or resume either work or break: `focus_start.pcm` plays once.
5. Pause either work or break: `menu_confirm.pcm` plays once.
6. Let either phase reach zero: `focus_complete.pcm` plays once.
7. Set `SOUND OFF`: current audio stops and queued audio is discarded.

## DMA repeat bug found during hardware testing

The first PCM build left the I2S channel enabled after a clip. On the real
MAX98357A path this repeated a short stale DMA fragment as a continuous
`tuk-tuk` until another clip replaced the buffer. The driver now enables I2S
immediately before each clip, sends one silence buffer, then disables I2S.
Hardware re-verification is still required after flashing this fix.

Before publishing or distributing the firmware, keep the license/source record
for every downloaded MP3 and verify that redistribution is allowed.

## What was learned

Short PCM clips avoid an MP3 decoder and make playback deterministic. Trimming
silence matters for responsive menu feedback, especially when the encoder is
turned quickly.
