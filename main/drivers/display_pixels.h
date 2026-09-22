#ifndef COPET_DISPLAY_PIXELS_H
#define COPET_DISPLAY_PIXELS_H

#include <stdint.h>

enum {
    /* Existing UI coordinates; these are NOT the physical panel dimensions. */
    COPET_DISPLAY_WIDTH = 240,
    COPET_DISPLAY_HEIGHT = 240,
    COPET_PANEL_WIDTH = 128,
    COPET_PANEL_HEIGHT = 160,
    COPET_VIEW_HEIGHT = 128,
    COPET_VIEW_Y = (COPET_PANEL_HEIGHT - COPET_VIEW_HEIGHT) / 2,
    COPET_PANEL_STRIPE_ROWS = 16,
};

/* Convert a physical stripe to RGB565 wire bytes (MSB first).
 * Source is a full 240x240 RGB332 frame. Destination has room for
 * COPET_PANEL_WIDTH * rows * 2 bytes. y/rows must be within the panel.
 * Preserves aspect ratio in a centered 128x128 viewport; other rows are black.
 */
void copet_display_convert_stripe(const uint8_t *frame, int y, int rows,
                                  uint8_t *destination);

#endif
