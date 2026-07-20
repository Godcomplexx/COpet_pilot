#include "ui/animation_ui.h"
#include "ui/ui_canvas.h"

void animation_ui_render(uint8_t *framebuffer, int width, int height,
                         const uint8_t *animation_data, size_t frame_index)
{
    if (framebuffer == NULL || animation_data == NULL ||
        width <= 0 || height <= 0) {
        return;
    }

    const ui_color_t shades[] = {
        ui_rgb332(0, 0, 0),
        ui_rgb332(95, 115, 130),
        ui_rgb332(190, 220, 235),
        ui_rgb332(255, 255, 255),
    };
    const size_t frame_bytes = (size_t)(width * height) / 4U;
    const uint8_t *frame = animation_data + frame_index * frame_bytes;

    for (size_t packed_index = 0; packed_index < frame_bytes; ++packed_index) {
        const uint8_t packed = frame[packed_index];
        const size_t pixel_index = packed_index * 4;
        framebuffer[pixel_index] = shades[(packed >> 6) & 0x03U];
        framebuffer[pixel_index + 1] = shades[(packed >> 4) & 0x03U];
        framebuffer[pixel_index + 2] = shades[(packed >> 2) & 0x03U];
        framebuffer[pixel_index + 3] = shades[packed & 0x03U];
    }
}
