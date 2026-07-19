# Boot screen learning log

Date: 2026-07-19

## What we are trying to prove

Replace the four-second LCD color diagnostic with a short product boot screen
that reports real local initialization progress and then opens Desk Mode.

## Hardware interface

The screen still uses the existing ST7789 SPI connection. The boot renderer
writes into the same RGB332 framebuffer, and `lcd_refresh()` converts it to
RGB565 stripes for the panel.

## Pin assumptions

No wiring changed: SCK D18, MOSI D5, DC D16 and RST D17.

## Smallest integration patch

`boot_ui_render()` draws the black-and-green terminal design. `app_main()`
updates it after display, input, sensor, network and local-service setup. The
only deliberate holds total 650 ms; Wi-Fi continues connecting in the
background after Desk Mode opens.

## How to verify

1. Reset or power-cycle the ESP32.
2. Confirm there are no red, green, blue or white full-screen test frames.
3. Confirm the progress labels end at `DESK MODE 100%`.
4. Confirm Desk Mode opens even when Wi-Fi is unavailable.

## Common failure cases

- A blank screen before the boot UI means the ST7789 initialization or wiring
  failed; the renderer cannot run until the framebuffer exists.
- A stage that disappears quickly is normal because it tracks local startup
  work instead of waiting several seconds artificially.
- Sensor and network failures remain recoverable and are reported in the serial
  log; they must not prevent Desk Mode from opening.

## What was learned

A product boot screen can expose useful initialization progress without keeping
the user inside a hardware diagnostic or waiting for online services.
