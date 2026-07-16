#include "ui/desk_ui.h"
#include "ui/pip_face_port.h"

#include <stddef.h>
#include <stdio.h>

typedef uint8_t color_t;

typedef struct {
    char character;
    uint8_t rows[5];
} glyph_t;

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
    {':', {0, 2, 0, 2, 0}}, {'%', {5, 1, 2, 4, 5}},
    {' ', {0, 0, 0, 0, 0}},
};

typedef struct {
    uint8_t *pixels;
    int width;
    int height;
} canvas_t;

static color_t rgb332(uint8_t red, uint8_t green, uint8_t blue)
{
    return (color_t)((red & 0xE0U) | ((green & 0xE0U) >> 3) |
                     (blue >> 6));
}

static void fill_rect(canvas_t *canvas, int x, int y, int width, int height,
                      color_t color)
{
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > canvas->width) { width = canvas->width - x; }
    if (y + height > canvas->height) { height = canvas->height - y; }
    if (width <= 0 || height <= 0) { return; }

    for (int row = y; row < y + height; ++row) {
        color_t *destination = &canvas->pixels[row * canvas->width + x];
        for (int column = 0; column < width; ++column) {
            destination[column] = color;
        }
    }
}

static void fill_round_rect(canvas_t *canvas, int x, int y, int width,
                            int height, int radius, color_t color)
{
    if (radius < 1) {
        fill_rect(canvas, x, y, width, height, color);
        return;
    }
    if (radius * 2 > width) { radius = width / 2; }
    if (radius * 2 > height) { radius = height / 2; }

    for (int row = 0; row < height; ++row) {
        int inset = 0;
        if (row < radius) {
            const int dy = radius - row - 1;
            while (inset < radius) {
                const int dx = radius - inset;
                if (dx * dx + dy * dy <= radius * radius) {
                    break;
                }
                ++inset;
            }
        } else if (row >= height - radius) {
            const int dy = row - (height - radius);
            while (inset < radius) {
                const int dx = radius - inset;
                if (dx * dx + dy * dy <= radius * radius) {
                    break;
                }
                ++inset;
            }
        }
        fill_rect(canvas, x + inset, y + row, width - inset * 2, 1, color);
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

static void draw_text(canvas_t *canvas, int x, int y, const char *text,
                      int scale, color_t color)
{
    while (*text != '\0') {
        const uint8_t *rows = find_glyph(*text++);
        for (int row = 0; row < 5; ++row) {
            for (int column = 0; column < 3; ++column) {
                if ((rows[row] & (1U << (2 - column))) != 0U) {
                    fill_rect(canvas, x + column * scale,
                              y + row * scale, scale, scale, color);
                }
            }
        }
        x += 4 * scale;
    }
}

static color_t comfort_color(desk_comfort_t comfort)
{
    switch (comfort) {
    case DESK_COMFORT_COLD: return rgb332(80, 170, 255);
    case DESK_COMFORT_DRY: return rgb332(255, 190, 70);
    case DESK_COMFORT_HUMID: return rgb332(125, 220, 255);
    case DESK_COMFORT_HOT: return rgb332(255, 105, 70);
    case DESK_COMFORT_COMFY: return rgb332(80, 245, 200);
    case DESK_COMFORT_SENSOR_ERROR:
    default: return rgb332(255, 90, 105);
    }
}

void desk_ui_render(uint8_t *framebuffer, int width, int height,
                    const desk_mode_view_t *view)
{
    if (framebuffer == NULL || view == NULL || width <= 0 || height <= 0) {
        return;
    }

    canvas_t canvas = {framebuffer, width, height};
    const color_t background = rgb332(6, 13, 27);
    const color_t panel = rgb332(12, 31, 53);
    const color_t panel_light = rgb332(18, 52, 76);
    const color_t white = rgb332(240, 250, 255);
    const color_t muted = rgb332(115, 155, 175);
    const color_t accent = comfort_color(view->comfort);

    fill_rect(&canvas, 0, 0, width, height, background);
    draw_text(&canvas, 12, 9, "DESK", 3, white);

    char line[24];
    const uint32_t uptime_minutes = view->uptime_seconds / 60U;
    snprintf(line, sizeof(line), "UP %02lu:%02lu",
             (unsigned long)(uptime_minutes / 60U),
             (unsigned long)(uptime_minutes % 60U));
    draw_text(&canvas, 128, 11, line, 2, muted);
    fill_rect(&canvas, 0, 28, width, 3, accent);

    fill_round_rect(&canvas, 10, 38, 220, 124, 16, panel);
    pip_face_port_render(framebuffer, width, height, 10, 38, 220, 124, view);

    fill_round_rect(&canvas, 10, 170, 106, 42, 8, panel_light);
    fill_round_rect(&canvas, 124, 170, 106, 42, 8, panel_light);
    draw_text(&canvas, 18, 177, "TEMP", 2, muted);
    draw_text(&canvas, 132, 177, "HUM", 2, muted);

    if (view->sensor_ok) {
        snprintf(line, sizeof(line), "%.1F C", (double)view->temperature_c);
        draw_text(&canvas, 18, 193, line, 2, white);
        snprintf(line, sizeof(line), "%.0F %%", (double)view->humidity_percent);
        draw_text(&canvas, 132, 193, line, 2, white);
    } else {
        draw_text(&canvas, 18, 193, "NO DATA", 2, accent);
        draw_text(&canvas, 132, 193, "CHECK", 2, accent);
    }

    draw_text(&canvas, 10, 222, desk_mode_expression_label(view->expression),
              2, accent);
    draw_text(&canvas, 112, 222, desk_mode_comfort_label(view->comfort),
              2, white);
    draw_text(&canvas, 184, 222, "MENU", 2, muted);
}
