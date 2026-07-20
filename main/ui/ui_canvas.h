#ifndef COPET_UI_CANVAS_H
#define COPET_UI_CANVAS_H

#include <stdint.h>

/*
 * Shared terminal-style drawing primitives used by every screen renderer.
 *
 * A canvas is a plain 8-bit RGB332 framebuffer plus its dimensions. Screens
 * build a canvas over the app framebuffer and draw into it; nothing here
 * touches ESP-IDF, so renderers stay portable and can be exercised on a host.
 */

typedef uint8_t ui_color_t;

typedef struct {
    uint8_t *pixels;
    int width;
    int height;
} ui_canvas_t;

ui_color_t ui_rgb332(uint8_t red, uint8_t green, uint8_t blue);

void ui_fill_rect(ui_canvas_t *canvas, int x, int y, int width, int height,
                  ui_color_t color);
void ui_draw_text(ui_canvas_t *canvas, int x, int y, const char *text,
                  int scale, ui_color_t color);
void ui_draw_dashed_horizontal(ui_canvas_t *canvas, int x, int y, int width,
                               ui_color_t color);
void ui_draw_dashed_vertical(ui_canvas_t *canvas, int x, int y, int height,
                             ui_color_t color);
/* Dashed rectangle border only. */
void ui_draw_terminal_outline(ui_canvas_t *canvas, int x, int y, int width,
                              int height, ui_color_t color);
/* Dashed border plus an inner 24px coordinate grid. */
void ui_draw_terminal_grid(ui_canvas_t *canvas, int x, int y, int width,
                           int height, ui_color_t color);
void ui_draw_corner_marks(ui_canvas_t *canvas, int x, int y, int width,
                          int height, ui_color_t color);
void ui_draw_scanlines(ui_canvas_t *canvas, ui_color_t color);

#endif
