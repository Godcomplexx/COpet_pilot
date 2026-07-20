#include "ui/menu_ui.h"
#include "ui/pip_face_port.h"
#include "ui/ui_canvas.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

void menu_ui_render(uint8_t *framebuffer, int width, int height,
                    const menu_mode_t *menu,
                    const copet_behavior_view_t *behavior,
                    const char *network_status)
{
    if (framebuffer == NULL || menu == NULL || width <= 0 || height <= 0) {
        return;
    }

    ui_canvas_t canvas = {framebuffer, width, height};
    const ui_color_t background = ui_rgb332(0, 0, 0);
    const ui_color_t phosphor = ui_rgb332(150, 255, 70);
    const ui_color_t pale = ui_rgb332(205, 255, 145);
    const ui_color_t muted = ui_rgb332(65, 170, 60);
    const ui_color_t grid = ui_rgb332(0, 65, 0);
    const ui_color_t scanline = ui_rgb332(0, 32, 0);

    ui_fill_rect(&canvas, 0, 0, width, height, background);
    ui_draw_scanlines(&canvas, scanline);
    ui_draw_text(&canvas, 8, 6, "COPET MENU", 3, phosphor);

    const size_t count = menu_mode_count();
    char line[16];
    snprintf(line, sizeof(line), "%02u/%02u",
             (unsigned)(menu->selected + 1U), (unsigned)count);
    if (behavior != NULL && behavior->id != COPET_BEHAVIOR_NEUTRAL) {
        ui_fill_rect(&canvas, 172, 3, 62, 26, background);
        pip_face_port_render_compact(framebuffer, width, height,
                                     176, 4, 54, 24, behavior);
    } else {
        ui_draw_text(&canvas, 176, 8, line, 2, muted);
    }
    ui_draw_text(&canvas, 8, 27, "SYSTEM", 2, muted);
    ui_draw_text(&canvas, 136, 27,
                 network_status != NULL ? network_status : "WIFI OFF",
                 2, muted);
    ui_draw_dashed_horizontal(&canvas, 6, 42, 228, grid);

    for (size_t index = 0; index < count; ++index) {
        const menu_item_t *item = menu_mode_item(index);
        const int y = 47 + (int)index * 50;
        const bool selected = index == menu->selected;
        if (selected) {
            ui_fill_rect(&canvas, 6, y, 228, 44, phosphor);
        } else {
            ui_draw_terminal_outline(&canvas, 6, y, 228, 44, grid);
        }
        ui_draw_text(&canvas, 14, y + 6, ">", 3, selected ? background : muted);
        ui_draw_text(&canvas, 30, y + 6, item->label, 3,
                     selected ? background : pale);
        ui_draw_text(&canvas, 30, y + 25, item->detail, 2,
                     selected ? background : muted);
    }

    ui_draw_dashed_horizontal(&canvas, 6, 201, 228, grid);
    ui_draw_text(&canvas, 8, 207, "TURN SELECT", 2, phosphor);
    ui_draw_text(&canvas, 144, 207, "TOUCH OPEN", 2, phosphor);
    ui_draw_text(&canvas, 8, 227, "HOLD HOME", 2, muted);
    ui_draw_text(&canvas, 128, 227, "10S AUTO HOME", 2, muted);
}
