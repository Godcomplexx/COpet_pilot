# Phase 06 — SD Card and Mini TV

## Goal

Use SD card for local media: pictures, sounds, short clips.

## Learn

- SPI shared bus
- SD initialization
- FAT filesystem
- file list
- buffering
- media format constraints

## Minimal experiments

1. Mount SD card.
2. List files.
3. Read text file.
4. Display image.
5. Play WAV.
6. Play short image sequence/MJPEG.

## Recommendation

Do not try YouTube on ESP32-S3.  
Use local clips on SD card.

Good first format:

```text
MJPEG 240×240, 10–15 fps
WAV audio
```

## Definition of done

- SD card mounts;
- video list appears in menu;
- one clip plays;
- errors are displayed if SD missing.
