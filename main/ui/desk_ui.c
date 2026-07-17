#include "ui/desk_ui.h"
#include "ui/pip_face_port.h"
#include "ui/ui_canvas.h"

#include <stddef.h>
#include <stdio.h>

static ui_color_t comfort_color(desk_comfort_t comfort)
{
    switch (comfort) {
    case DESK_COMFORT_COLD: return ui_rgb332(90, 230, 150);
    case DESK_COMFORT_DRY: return ui_rgb332(210, 250, 65);
    case DESK_COMFORT_HUMID: return ui_rgb332(85, 220, 100);
    case DESK_COMFORT_HOT: return ui_rgb332(225, 250, 60);
    case DESK_COMFORT_COMFY: return ui_rgb332(150, 255, 70);
    case DESK_COMFORT_SENSOR_ERROR:
    default: return ui_rgb332(255, 115, 65);
    }
}

void desk_ui_render(uint8_t *framebuffer, int width, int height,
                    const desk_mode_view_t *view,
                    const char *network_status,
                    const weather_service_snapshot_t *weather,
                    desk_ui_environment_t environment)
{
    if (framebuffer == NULL || view == NULL || width <= 0 || height <= 0) {
        return;
    }

    ui_canvas_t canvas = {framebuffer, width, height};
    const ui_color_t background = ui_rgb332(0, 0, 0);
    const ui_color_t phosphor = ui_rgb332(150, 255, 70);
    const ui_color_t pale = ui_rgb332(205, 255, 145);
    const ui_color_t muted = ui_rgb332(65, 170, 60);
    const ui_color_t grid = ui_rgb332(0, 65, 0);
    const ui_color_t scanline = ui_rgb332(0, 32, 0);
    const ui_color_t accent = comfort_color(view->comfort);

    ui_fill_rect(&canvas, 0, 0, width, height, background);
    ui_draw_scanlines(&canvas, scanline);
    ui_draw_text(&canvas, 8, 6, "COPET DESK", 2, phosphor);

    char line[24];
    ui_draw_text(&canvas, 160, 6,
                 network_status != NULL ? network_status : "WIFI OFF",
                 2, muted);
    ui_draw_dashed_horizontal(&canvas, 6, 20, 228, grid);

    ui_draw_terminal_grid(&canvas, 5, 24, 230, 152, grid);
    ui_draw_corner_marks(&canvas, 5, 24, 230, 152, accent);
    pip_face_port_render(framebuffer, width, height, 7, 26, 226, 148, view);

    const bool show_outdoor = environment == DESK_UI_ENVIRONMENT_OUTDOOR;
    ui_draw_dashed_horizontal(&canvas, 6, 179, 228, grid);
    ui_draw_text(&canvas, 8, 183,
                 show_outdoor ? ">OUT DATA" : ">ROOM DATA", 2, phosphor);
    ui_draw_text(&canvas, 152, 183,
                 show_outdoor ? "TOUCH>ROOM" : "TOUCH>OUT", 2, muted);

    const bool values_available = show_outdoor
        ? weather != NULL && weather->has_data
        : view->sensor_ok;
    if (values_available) {
        const float temperature = show_outdoor
            ? weather->temperature_c
            : view->temperature_c;
        const float humidity = show_outdoor
            ? weather->humidity_percent
            : view->humidity_percent;
        snprintf(line, sizeof(line), "%.1FC", (double)temperature);
        ui_draw_text(&canvas, 12, 198, line, 3, pale);
        snprintf(line, sizeof(line), "%.1F%%", (double)humidity);
        ui_draw_text(&canvas, 132, 198, line, 3, pale);
    } else {
        const char *status = show_outdoor && weather != NULL
            ? weather_service_status_label(weather->status)
            : "NO DATA";
        ui_draw_text(&canvas, 12, 198, status, 3, accent);
        ui_draw_text(&canvas, 132, 198, "--", 3, accent);
    }
    ui_draw_text(&canvas, 12, 216, "TEMP", 2, muted);
    ui_draw_text(&canvas, 132, 216, "HUM", 2, muted);
    ui_draw_dashed_vertical(&canvas, 119, 195, 29, grid);

    ui_draw_text(&canvas, 8, 230,
                 desk_mode_expression_label(view->expression), 2, accent);
    ui_draw_text(&canvas, 96, 230, desk_mode_comfort_label(view->comfort),
                 2, pale);
    ui_draw_text(&canvas, 200, 230, "MENU", 2, phosphor);
}
