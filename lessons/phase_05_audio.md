# Phase 05 — Audio

## Goal

Add sound feedback without blocking UI.

## Learn

- GPIO buzzer vs I2S audio
- amplifier module
- WAV format
- sample rate
- non-blocking playback idea

## Recommended path

1. Simple beep first.
2. Then I2S amplifier.
3. Then WAV playback.
4. Then Mini TV audio.

## Important

Do not connect speaker directly to ESP32 GPIO.  
Use an amplifier: MAX98357A, PAM8302, PAM8403, or similar.

## Definition of done

- menu select sound works;
- focus complete sound works;
- sound does not freeze menu.
