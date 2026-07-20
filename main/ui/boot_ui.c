#include "ui/boot_ui.h"
#include "ui/ui_canvas.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int centered_text_x(int width, const char *text, int scale)
{
    const int text_width = (int)strlen(text) * 4 * scale;
    return (width - text_width) / 2;
}

void boot_ui_render(uint8_t *framebuffer, int width, int height,
                    uint8_t progress_percent, const char *status)
{
    if (framebuffer == NULL || width <= 0 || height <= 0) {
        return;
    }

    const uint8_t progress =
        progress_percent <= 100U ? progress_percent : 100U;
    const char *label = status != NULL ? status : "INITIALIZING";
    ui_canvas_t canvas = {framebuffer, width, height};
    const ui_color_t background = ui_rgb332(0, 0, 0);
    const ui_color_t phosphor = ui_rgb332(150, 255, 70);
    const ui_color_t pale = ui_rgb332(205, 255, 145);
    const ui_color_t muted = ui_rgb332(65, 170, 60);
    const ui_color_t grid = ui_rgb332(0, 65, 0);
    const ui_color_t scanline = ui_rgb332(0, 32, 0);

    ui_fill_rect(&canvas, 0, 0, width, height, background);
    ui_draw_scanlines(&canvas, scanline);
    ui_draw_text(&canvas, 8, 6, "COPET SYSTEM", 3, phosphor);
    ui_draw_text(&canvas, 200, 8, "BOOT", 2, muted);
    ui_draw_dashed_horizontal(&canvas, 6, 29, 228, grid);

    ui_draw_corner_marks(&canvas, 16, 42, 208, 156, muted);
    ui_draw_text(&canvas, centered_text_x(width, "COPET", 6), 57,
                 "COPET", 6, pale);
    ui_draw_text(&canvas,
                 centered_text_x(width, "DESK COMPANION", 2), 94,
                 "DESK COMPANION", 2, muted);

    ui_draw_terminal_outline(&canvas, 24, 124, 192, 28, grid);
    const int progress_width = (180 * progress) / 100;
    ui_fill_rect(&canvas, 30, 131, progress_width, 14, phosphor);

    ui_draw_text(&canvas, centered_text_x(width, label, 2), 164,
                 label, 2, phosphor);
    char percent[8];
    snprintf(percent, sizeof(percent), "%03u%%", (unsigned)progress);
    ui_draw_text(&canvas, centered_text_x(width, percent, 3), 181,
                 percent, 3, pale);

    ui_draw_dashed_horizontal(&canvas, 6, 211, 228, grid);
    ui_draw_text(&canvas, 32, 220, "> LOCAL SYSTEM START", 2, muted);
}
