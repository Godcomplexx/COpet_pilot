#ifndef COPET_ANIMATION_UI_H
#define COPET_ANIMATION_UI_H

#include <stddef.h>
#include <stdint.h>

/* Packed 2 bits-per-pixel bytes for a 240x240 frame. */
enum {
    ANIMATION_FRAME_BYTES = 240 * 240 / 4,
};

/*
 * Unpack one 2bpp frame from the embedded animation blob into the framebuffer.
 * The blob is a flat array of frames; frame_index selects which one.
 */
void animation_ui_render(uint8_t *framebuffer, int width, int height,
                         const uint8_t *animation_data, size_t frame_index);

#endif
