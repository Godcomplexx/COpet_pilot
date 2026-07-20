#include "ui/focus_ui.h"
#include "ui/ui_canvas.h"

#include <stddef.h>
#include <stdio.h>

void focus_ui_render(uint8_t *framebuffer, int width, int height,
                     const focus_mode_t *focus)
{
    if (framebuffer == NULL || focus == NULL || width <= 0 || height <= 0) {
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
    ui_draw_text(&canvas, 8, 6, "FOCUS MODE", 3, phosphor);
    ui_draw_text(&canvas, 192, 8, focus_mode_preset_label(focus), 2,
                 focus->state == FOCUS_TIMER_READY ? phosphor : muted);
    ui_draw_dashed_horizontal(&canvas, 6, 29, 228, grid);

    if (focus->state == FOCUS_TIMER_RUNNING) {
        ui_fill_rect(&canvas, 6, 37, 228, 32, phosphor);
        ui_draw_text(&canvas, 14, 46, ">", 3, background);
        ui_draw_text(&canvas, 30, 46, focus_mode_status_label(focus), 3,
                     background);
    } else {
        ui_draw_terminal_outline(&canvas, 6, 37, 228, 32, grid);
        ui_draw_text(&canvas, 14, 46, ">", 3, muted);
        ui_draw_text(&canvas, 30, 46, focus_mode_status_label(focus), 3, pale);
    }

    ui_draw_terminal_outline(&canvas, 6, 77, 228, 94, grid);
    for (int x = 42; x < 234; x += 36) {
        ui_draw_dashed_vertical(&canvas, x, 77, 94, grid);
    }
    for (int y = 101; y < 171; y += 24) {
        ui_draw_dashed_horizontal(&canvas, 6, y, 228, grid);
    }
    ui_draw_text(&canvas, 12, 83, "REMAINING", 2, muted);

    char line[20];
    const uint32_t minutes = focus->remaining_seconds / 60U;
    const uint32_t seconds = focus->remaining_seconds % 60U;
    snprintf(line, sizeof(line), "%02lu:%02lu",
             (unsigned long)minutes, (unsigned long)seconds);
    ui_draw_text(&canvas, 40, 104, line, 8, pale);

    const uint32_t total_seconds = focus->break_phase
        ? focus_mode_break_seconds(focus)
        : focus_mode_work_seconds(focus);
    const uint32_t bounded_remaining =
        focus->remaining_seconds < total_seconds
            ? focus->remaining_seconds
            : total_seconds;
    const int progress_width =
        (int)((uint64_t)212U * bounded_remaining / total_seconds);
    ui_fill_rect(&canvas, 12, 157, 216, 8, grid);
    ui_fill_rect(&canvas, 14, 159, progress_width, 4, phosphor);

    snprintf(line, sizeof(line), "SESSIONS %lu",
             (unsigned long)focus->sessions);
    ui_draw_text(&canvas, 8, 180, line, 2, phosphor);
    ui_draw_text(&canvas, 144, 180,
                 focus->break_phase ? "PHASE BREAK" : "PHASE WORK", 2, muted);

    ui_draw_dashed_horizontal(&canvas, 6, 201, 228, grid);
    ui_draw_text(&canvas, 8, 208, focus_mode_action_hint(focus), 2, phosphor);
    ui_draw_text(&canvas, 152, 208, "HOLD HOME", 2, phosphor);
    ui_draw_text(&canvas, 8, 228,
                 focus->state == FOCUS_TIMER_READY ? "TURN TIME" : "STATE SAVED",
                 2, muted);
    const uint32_t phase_minutes = (focus->break_phase
        ? focus_mode_break_seconds(focus)
        : focus_mode_work_seconds(focus)) / 60U;
    snprintf(line, sizeof(line), "%lu MIN", (unsigned long)phase_minutes);
    ui_draw_text(&canvas, 184, 228, line, 2, muted);
}
