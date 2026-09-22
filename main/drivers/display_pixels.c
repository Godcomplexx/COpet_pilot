#include "drivers/display_pixels.h"

void copet_display_convert_stripe(const uint8_t *frame, int y, int rows,
                                  uint8_t *destination)
{
    for (int row = 0; row < rows; ++row) {
        const int view_y = y + row - COPET_VIEW_Y;
        if (view_y < 0 || view_y >= COPET_VIEW_HEIGHT) {
            for (int x = 0; x < COPET_PANEL_WIDTH; ++x) {
                *destination++ = 0;
                *destination++ = 0;
            }
            continue;
        }
        /* Endpoint-aligned nearest neighbour includes all four UI edges. */
        const int source_y = view_y * (COPET_DISPLAY_HEIGHT - 1) /
                             (COPET_VIEW_HEIGHT - 1);
        for (int x = 0; x < COPET_PANEL_WIDTH; ++x) {
            const int source_x = x * (COPET_DISPLAY_WIDTH - 1) /
                                 (COPET_PANEL_WIDTH - 1);
            const uint8_t color = frame[source_y * COPET_DISPLAY_WIDTH + source_x];
            const unsigned r = (color >> 5) & 7;
            const unsigned g = (color >> 2) & 7;
            const unsigned b = color & 3;
            const uint16_t rgb565 = (((r << 2) | (r >> 1)) << 11) |
                                    (((g << 3) | g) << 5) |
                                    ((b << 3) | (b << 1) | (b >> 1));
            *destination++ = (uint8_t)(rgb565 >> 8);
            *destination++ = (uint8_t)rgb565;
        }
    }
}
