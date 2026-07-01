# Phase 02 — ST7789 Display

## Goal

Make ST7789 240×240 SPI display show text and a simple CoPet face.

## Learn

- SPI bus
- SCK/MOSI/CS/DC/RST/BLK
- display initialization
- command vs data
- screen coordinates
- color formats RGB565

## Minimal experiment

1. Fill screen black.
2. Draw white rectangle.
3. Print `CoPet Boot OK`.
4. Draw simple face.

## Questions to answer

- What does DC pin do?
- Why does BLK matter?
- What is RGB565?
- Why do some screens need color inversion?

## Definition of done

- screen is not white/blank;
- text visible;
- simple face drawn;
- display pins documented.
