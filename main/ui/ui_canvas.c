#include "ui/ui_canvas.h"

#include <stddef.h>

typedef struct {
    char character;
    uint8_t rows[5];
} glyph_t;

/*
 * Compact 3x5 font. Each row uses the low three bits. This is the union of the
 * glyphs every screen needs; the trailing space is the fallback for any
 * character that is not listed.
 */
static const glyph_t FONT[] = {
    {'A', {2, 5, 7, 5, 5}}, {'B', {6, 5, 6, 5, 6}},
    {'C', {3, 4, 4, 4, 3}}, {'D', {6, 5, 5, 5, 6}},
    {'E', {7, 4, 6, 4, 7}}, {'F', {7, 4, 6, 4, 4}},
    {'G', {3, 4, 5, 5, 3}}, {'H', {5, 5, 7, 5, 5}},
    {'I', {7, 2, 2, 2, 7}}, {'J', {1, 1, 1, 5, 2}},
    {'K', {5, 5, 6, 5, 5}}, {'L', {4, 4, 4, 4, 7}},
    {'M', {5, 7, 7, 5, 5}}, {'N', {5, 7, 7, 7, 5}},
    {'O', {2, 5, 5, 5, 2}}, {'P', {6, 5, 6, 4, 4}},
    {'Q', {2, 5, 5, 3, 1}}, {'R', {6, 5, 6, 5, 5}},
    {'S', {3, 4, 2, 1, 6}}, {'T', {7, 2, 2, 2, 2}},
    {'U', {5, 5, 5, 5, 7}}, {'V', {5, 5, 5, 5, 2}},
    {'W', {5, 5, 7, 7, 5}}, {'X', {5, 5, 2, 5, 5}},
    {'Y', {5, 5, 2, 2, 2}}, {'Z', {7, 1, 2, 4, 7}},
    {'0', {7, 5, 5, 5, 7}}, {'1', {2, 6, 2, 2, 7}},
    {'2', {6, 1, 7, 4, 7}}, {'3', {6, 1, 3, 1, 6}},
    {'4', {5, 5, 7, 1, 1}}, {'5', {7, 4, 6, 1, 6}},
    {'6', {3, 4, 7, 5, 7}}, {'7', {7, 1, 2, 2, 2}},
    {'8', {7, 5, 7, 5, 7}}, {'9', {7, 5, 7, 1, 6}},
    {'-', {0, 0, 7, 0, 0}}, {'.', {0, 0, 0, 0, 2}},
    {':', {0, 2, 0, 2, 0}}, {'>', {4, 2, 1, 2, 4}},
    {'/', {1, 1, 2, 4, 4}}, {'%', {5, 1, 2, 4, 5}},
    {'(', {1, 2, 2, 2, 1}}, {')', {4, 2, 2, 2, 4}},
    {'[', {6, 4, 4, 4, 6}}, {']', {3, 1, 1, 1, 3}},
    {' ', {0, 0, 0, 0, 0}},
};

ui_color_t ui_rgb332(uint8_t red, uint8_t green, uint8_t blue)
{
    return (ui_color_t)((red & 0xE0U) | ((green & 0xE0U) >> 3) |
                        (blue >> 6));
}

void ui_fill_rect(ui_canvas_t *canvas, int x, int y, int width, int height,
                  ui_color_t color)
{
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > canvas->width) { width = canvas->width - x; }
    if (y + height > canvas->height) { height = canvas->height - y; }
    if (width <= 0 || height <= 0) { return; }

    for (int row = y; row < y + height; ++row) {
        ui_color_t *destination = &canvas->pixels[row * canvas->width + x];
        for (int column = 0; column < width; ++column) {
            destination[column] = color;
        }
    }
}

static const uint8_t *find_glyph(char character)
{
    for (size_t index = 0; index < sizeof(FONT) / sizeof(FONT[0]); ++index) {
        if (FONT[index].character == character) {
            return FONT[index].rows;
        }
    }
    return FONT[sizeof(FONT) / sizeof(FONT[0]) - 1].rows;
}

void ui_draw_text(ui_canvas_t *canvas, int x, int y, const char *text,
                  int scale, ui_color_t color)
{
    while (*text != '\0') {
        const uint8_t *rows = find_glyph(*text++);
        for (int row = 0; row < 5; ++row) {
            for (int column = 0; column < 3; ++column) {
                if ((rows[row] & (1U << (2 - column))) != 0U) {
                    ui_fill_rect(canvas, x + column * scale,
                                 y + row * scale, scale, scale, color);
                }
            }
        }
        x += 4 * scale;
    }
}

void ui_draw_dashed_horizontal(ui_canvas_t *canvas, int x, int y, int width,
                               ui_color_t color)
{
    for (int offset = 0; offset < width; offset += 6) {
        const int dash_width = offset + 3 <= width ? 3 : width - offset;
        ui_fill_rect(canvas, x + offset, y, dash_width, 1, color);
    }
}

void ui_draw_dashed_vertical(ui_canvas_t *canvas, int x, int y, int height,
                             ui_color_t color)
{
    for (int offset = 0; offset < height; offset += 6) {
        const int dash_height = offset + 3 <= height ? 3 : height - offset;
        ui_fill_rect(canvas, x, y + offset, 1, dash_height, color);
    }
}

void ui_draw_terminal_outline(ui_canvas_t *canvas, int x, int y, int width,
                              int height, ui_color_t color)
{
    ui_draw_dashed_horizontal(canvas, x, y, width, color);
    ui_draw_dashed_horizontal(canvas, x, y + height - 1, width, color);
    ui_draw_dashed_vertical(canvas, x, y, height, color);
    ui_draw_dashed_vertical(canvas, x + width - 1, y, height, color);
}

void ui_draw_terminal_grid(ui_canvas_t *canvas, int x, int y, int width,
                           int height, ui_color_t color)
{
    for (int column = x + 24; column < x + width; column += 24) {
        ui_draw_dashed_vertical(canvas, column, y, height, color);
    }
    for (int row = y + 24; row < y + height; row += 24) {
        ui_draw_dashed_horizontal(canvas, x, row, width, color);
    }
    ui_draw_dashed_horizontal(canvas, x, y, width, color);
    ui_draw_dashed_horizontal(canvas, x, y + height - 1, width, color);
    ui_draw_dashed_vertical(canvas, x, y, height, color);
    ui_draw_dashed_vertical(canvas, x + width - 1, y, height, color);
}

void ui_draw_corner_marks(ui_canvas_t *canvas, int x, int y, int width,
                          int height, ui_color_t color)
{
    const int mark = 9;
    ui_fill_rect(canvas, x, y, mark, 1, color);
    ui_fill_rect(canvas, x, y, 1, mark, color);
    ui_fill_rect(canvas, x + width - mark, y, mark, 1, color);
    ui_fill_rect(canvas, x + width - 1, y, 1, mark, color);
    ui_fill_rect(canvas, x, y + height - 1, mark, 1, color);
    ui_fill_rect(canvas, x, y + height - mark, 1, mark, color);
    ui_fill_rect(canvas, x + width - mark, y + height - 1, mark, 1, color);
    ui_fill_rect(canvas, x + width - 1, y + height - mark, 1, mark, color);
}

void ui_draw_scanlines(ui_canvas_t *canvas, ui_color_t color)
{
    for (int y = 3; y < canvas->height; y += 4) {
        for (int x = 0; x < canvas->width; x += 2) {
            ui_fill_rect(canvas, x, y, 1, 1, color);
        }
    }
}
