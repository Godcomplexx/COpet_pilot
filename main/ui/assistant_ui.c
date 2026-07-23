#include "ui/assistant_ui.h"
#include "ui/ui_canvas.h"

#include <stddef.h>
#include <string.h>

int assistant_ui_wrap(const char *text, int max_chars,
                      char lines[][ASSISTANT_UI_LINE_MAX], int max_lines)
{
    if (text == NULL || lines == NULL || max_lines <= 0) { return 0; }
    if (max_chars < 1) { max_chars = 1; }
    if (max_chars > ASSISTANT_UI_LINE_MAX - 1) {
        max_chars = ASSISTANT_UI_LINE_MAX - 1;
    }

    int line_count = 0;
    int col = 0;
    lines[0][0] = '\0';

    size_t i = 0;
    while (text[i] != '\0' && line_count < max_lines) {
        /* Skip runs of spaces between words. */
        if (text[i] == ' ') {
            ++i;
            continue;
        }
        /* Measure the next word. */
        size_t start = i;
        while (text[i] != '\0' && text[i] != ' ') { ++i; }
        int word_len = (int)(i - start);

        /* If the word does not fit on the current line, wrap first (unless the
         * line is empty, in which case the word will be hard-split below). */
        if (col > 0 && col + 1 + word_len > max_chars) {
            lines[line_count][col] = '\0';
            if (++line_count >= max_lines) { break; }
            col = 0;
            lines[line_count][0] = '\0';
        }

        if (col > 0) {
            lines[line_count][col++] = ' ';
        }

        for (int w = 0; w < word_len; ++w) {
            if (col >= max_chars) { /* hard-split a long word */
                lines[line_count][col] = '\0';
                if (++line_count >= max_lines) { col = 0; break; }
                col = 0;
                lines[line_count][0] = '\0';
            }
            lines[line_count][col++] = text[start + (size_t)w];
        }
    }

    if (line_count < max_lines) {
        lines[line_count][col] = '\0';
        if (col > 0 || line_count == 0) { ++line_count; }
    }
    return line_count;
}

static void draw_body_lines(ui_canvas_t *canvas, const char *text,
                            int top_y, ui_color_t color)
{
    char lines[ASSISTANT_UI_MAX_LINES][ASSISTANT_UI_LINE_MAX];
    const int count = assistant_ui_wrap(text, 26, lines,
                                        ASSISTANT_UI_MAX_LINES);
    for (int i = 0; i < count; ++i) {
        ui_draw_text(canvas, 12, top_y + i * 18, lines[i], 2, color);
    }
}

void assistant_ui_render(uint8_t *framebuffer, int width, int height,
                         const assistant_mode_t *assistant)
{
    if (framebuffer == NULL || assistant == NULL || width <= 0 ||
        height <= 0) {
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
    ui_draw_text(&canvas, 8, 6, "ASSISTANT", 3, phosphor);
    ui_draw_text(&canvas, 176, 8, assistant_mode_state_label(assistant->state),
                 2, muted);
    ui_draw_dashed_horizontal(&canvas, 6, 29, 228, grid);

    const assistant_preset_t *preset =
        assistant_mode_selected_preset(assistant);
    ui_draw_text(&canvas, 8, 38, "Q:", 2, muted);
    ui_draw_text(&canvas, 32, 38, preset != NULL ? preset->label : "-", 2,
                 pale);

    ui_draw_terminal_outline(&canvas, 6, 58, 228, 140, grid);

    switch (assistant->state) {
    case ASSISTANT_RESULT:
        draw_body_lines(&canvas, assistant->result_text, 68, pale);
        if (assistant->result_mood[0] != '\0') {
            ui_draw_text(&canvas, 12, 176, "MOOD:", 2, muted);
            ui_draw_text(&canvas, 68, 176, assistant->result_mood, 2, phosphor);
        }
        break;
    case ASSISTANT_ERROR:
        ui_draw_text(&canvas, 12, 68, "ERROR", 3, phosphor);
        draw_body_lines(&canvas, assistant->result_text, 100, pale);
        break;
    case ASSISTANT_WAITING:
        ui_draw_text(&canvas, 12, 110, "WAITING...", 3, phosphor);
        break;
    case ASSISTANT_IDLE:
    default:
        ui_draw_text(&canvas, 12, 74, "ASK:", 2, muted);
        ui_draw_text(&canvas, 12, 96, preset != NULL ? preset->label : "-", 4,
                     phosphor);
        ui_draw_text(&canvas, 12, 150, "TURN TO PICK", 2, muted);
        break;
    }

    ui_draw_dashed_horizontal(&canvas, 6, 205, 228, grid);
    const char *hint =
        assistant->state == ASSISTANT_IDLE ? "TAP ASK" :
        assistant->state == ASSISTANT_WAITING ? "PLEASE WAIT" : "TAP BACK";
    ui_draw_text(&canvas, 8, 212, hint, 2, phosphor);
    ui_draw_text(&canvas, 152, 212, "HOLD HOME", 2, muted);
}
