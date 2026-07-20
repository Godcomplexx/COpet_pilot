# Aseprite animation learning log

Date: 2026-07-16

Current status: the verified gallery implementation and all source assets stay
in the repository, but the mode and its 100,800-byte asset are excluded from
the product firmware until the SD-card media stage.

## What we are trying to prove

Display a seven-frame Aseprite animation on the ST7789 without exhausting the
ESP32-WROOM-32 application partition.

## Asset pipeline

```text
animation/*.png
→ tools/convert_animation.ps1
→ 2-bit grayscale embedded asset
→ RGB332 framebuffer
→ RGB565 DMA stripes
→ ST7789
```

Each source frame is 240x240. Four grayscale pixels are stored in one byte, so
one frame occupies 14,400 bytes and all seven frames occupy 100,800 bytes.
The two intermediate levels are rendered with a blue-gray palette so the
liquid remains visible without making the black outline lighter.

## Verification

The steps below describe the original standalone hardware verification. To run
them again, the gallery sources and embedded asset must first be re-enabled in
`main/CMakeLists.txt` and the product menu.

1. Run `tools/convert_animation.ps1` after changing the PNG frames.
2. Build and flash the firmware.
3. Select `ANIMATION` in the menu with the encoder.
4. Touch briefly to open it.
5. Confirm all seven frames repeat approximately every 200 ms.
6. Hold touch for one second to return to the menu.

## Common failures

- Build reports an incorrect asset size: rerun the conversion script.
- Image has the wrong dimensions: export every frame as exactly 240x240.
- Animation is too fast for the display: increase
  `ANIMATION_FRAME_INTERVAL_US`.
- Grayscale detail is lost: use stronger contrast in the Aseprite source.

## What was learned

Source PNG files are convenient for drawing but are not the runtime format.
Offline conversion keeps the firmware decoder small and makes flash usage
predictable.
