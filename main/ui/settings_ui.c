#include "ui/settings_ui.h"
#include "ui/ui_canvas.h"

#include <stddef.h>

void settings_ui_render(uint8_t *framebuffer, int width, int height,
                        const desk_mode_view_t *view,
                        const settings_mode_t *settings,
                        const char *network_status,
                        const weather_service_snapshot_t *weather)
{
    if (framebuffer == NULL || width <= 0 || height <= 0) {
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
    ui_draw_text(&canvas, 8, 6, "SETTINGS", 3, phosphor);
    ui_draw_dashed_horizontal(&canvas, 6, 30, 228, grid);

    const char *weather_status = weather == NULL
        ? "OFF"
        : weather_service_status_label(weather->status);
    const char *sensor_status = view != NULL && view->sensor_ok
        ? "READY"
        : "NO DATA";
    static const char *const labels[] = {
        "NETWORK", "WEATHER", "ROOM SENSOR", "SOUND",
    };
    const char *const values[] = {
        network_status != NULL ? network_status : "WIFI OFF",
        weather_status, sensor_status, settings_mode_sound_label(settings),
    };
    for (size_t index = 0; index < 4; ++index) {
        const int y = 42 + (int)index * 36;
        ui_draw_terminal_outline(&canvas, 6, y, 228, 30, grid);
        ui_draw_text(&canvas, 14, y + 10, labels[index], 2, muted);
        ui_draw_text(&canvas, 132, y + 10, values[index], 2, pale);
    }

    ui_draw_dashed_horizontal(&canvas, 6, 195, 228, grid);
    ui_draw_text(&canvas, 8, 204, "TOUCH SOUND", 2, muted);
    ui_draw_text(&canvas, 152, 204, "HOLD HOME", 2, phosphor);
    ui_draw_text(&canvas, 8, 225, "RUNTIME SETTING", 2, muted);
}
