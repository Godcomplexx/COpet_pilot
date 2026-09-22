#ifndef COPET_DISPLAY_H
#define COPET_DISPLAY_H

#include <stdint.h>

#include "esp_err.h"
#include "drivers/display_pixels.h"

/*
 * ST7735/ST7735S 128x160 SPI display (1.77-inch eight-pin module).
 *
 * Owns the SPI bus, panel handle, an 8-bit RGB332 framebuffer and the DMA
 * transfer path. The UI layer draws into the framebuffer returned by
 * copet_display_framebuffer(); copet_display_refresh() converts it to RGB565
 * and pushes it out one DMA stripe at a time.
 *
 * Wiring: SCK=18, MOSI=5, DC/RS=16, RST/RES=17, CS tied to GND.
 * The UI stays at 240x240 and is scaled to a centered 128x128 viewport.
 */

/* Bring up SPI, the ST7735 panel and allocate the framebuffers. */
esp_err_t copet_display_init(void);

/* Pointer to the WIDTH*HEIGHT RGB332 framebuffer the UI draws into. Valid only
 * after a successful copet_display_init(). */
uint8_t *copet_display_framebuffer(void);

/* Push the current framebuffer to the panel (blocks until the last stripe is
 * transferred). */
esp_err_t copet_display_refresh(void);

/* Diagnostic build only: native RGB565 fills and a stationary test pattern. */
esp_err_t copet_display_diagnostic(void);

#endif
