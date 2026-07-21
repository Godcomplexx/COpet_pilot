#ifndef COPET_DISPLAY_H
#define COPET_DISPLAY_H

#include <stdint.h>

#include "esp_err.h"

/*
 * ST7789 240x240 SPI display (the GMT130 board in the photos).
 *
 * Owns the SPI bus, panel handle, an 8-bit RGB332 framebuffer and the DMA
 * transfer path. The UI layer draws into the framebuffer returned by
 * copet_display_framebuffer(); copet_display_refresh() converts it to RGB565
 * and pushes it out one DMA stripe at a time.
 *
 * Wiring: SCK=18, MOSI=5, DC=16, RST=17 (no CS).
 */

enum {
    COPET_DISPLAY_WIDTH = 240,
    COPET_DISPLAY_HEIGHT = 240,
};

/* Bring up SPI, the ST7789 panel and allocate the framebuffers. */
esp_err_t copet_display_init(void);

/* Pointer to the WIDTH*HEIGHT RGB332 framebuffer the UI draws into. Valid only
 * after a successful copet_display_init(). */
uint8_t *copet_display_framebuffer(void);

/* Push the current framebuffer to the panel (blocks until the last stripe is
 * transferred). */
esp_err_t copet_display_refresh(void);

#endif
