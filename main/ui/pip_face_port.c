/*
 * ESP32/ST7789 adaptation of the procedural eye mechanics from:
 * https://github.com/HamzaYslmn/esp-bridge-mcp-robot
 * Upstream revision used for the port: 345bc4a83cf6c1c05de0d5d378b479bec42cf86b
 *
 * Originally created by Hamza Yeşilmen (HamzaYslmn).
 * Source:  https://github.com/HamzaYslmn/
 * Sponsor: https://github.com/sponsors/HamzaYslmn
 *
 * This file is a modified C port for CoPet Pilot. It replaces PIL/OLED drawing
 * with bounded RGB332 framebuffer primitives and scales 128x64 coordinates to
 * a 220x124 ST7789 face area. See THIRD_PARTY_LICENSES/esp-bridge-mcp-robot-LICENSE.txt.
 */

#include "ui/pip_face_port.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

typedef uint8_t color_t;

typedef struct {
    uint8_t *pixels;
    int screen_width;
    int screen_height;
    int x;
    int y;
    int width;
    int height;
} face_canvas_t;

enum {
    LOGICAL_WIDTH = 128,
    LOGICAL_HEIGHT = 64,
};

static color_t rgb332(uint8_t red, uint8_t green, uint8_t blue)
{
    return (color_t)((red & 0xE0U) | ((green & 0xE0U) >> 3) |
                     (blue >> 6));
}

static int map_x(const face_canvas_t *canvas, float x)
{
    return canvas->x + (int)(x * canvas->width / LOGICAL_WIDTH);
}

static int map_y(const face_canvas_t *canvas, float y)
{
    return canvas->y + (int)(y * canvas->height / LOGICAL_HEIGHT);
}

static void fill_rect_physical(face_canvas_t *canvas, int x, int y,
                               int width, int height, color_t color)
{
    const int clip_right = canvas->x + canvas->width;
    const int clip_bottom = canvas->y + canvas->height;
    if (x < canvas->x) { width -= canvas->x - x; x = canvas->x; }
    if (y < canvas->y) { height -= canvas->y - y; y = canvas->y; }
    if (x + width > clip_right) {
        width = clip_right - x;
    }
    if (y + height > clip_bottom) {
        height = clip_bottom - y;
    }
    if (width <= 0 || height <= 0) { return; }
    for (int row = y; row < y + height; ++row) {
        color_t *destination =
            &canvas->pixels[row * canvas->screen_width + x];
        for (int column = 0; column < width; ++column) {
            destination[column] = color;
        }
    }
}

static void fill_rect(face_canvas_t *canvas, float x, float y,
                      float width, float height, color_t color)
{
    const int x0 = map_x(canvas, x);
    const int y0 = map_y(canvas, y);
    const int x1 = map_x(canvas, x + width);
    const int y1 = map_y(canvas, y + height);
    fill_rect_physical(canvas, x0, y0, x1 - x0, y1 - y0, color);
}

static void fill_round_rect(face_canvas_t *canvas, float x, float y,
                            float width, float height, float radius,
                            color_t color)
{
    int x0 = map_x(canvas, x);
    int y0 = map_y(canvas, y);
    const int x1 = map_x(canvas, x + width);
    const int y1 = map_y(canvas, y + height);
    const int w = x1 - x0;
    const int h = y1 - y0;
    int r = map_y(canvas, radius) - canvas->y;
    if (r * 2 > w) { r = w / 2; }
    if (r * 2 > h) { r = h / 2; }
    for (int row = 0; row < h; ++row) {
        int inset = 0;
        const int dy = row < r ? r - row - 1 :
                       row >= h - r ? row - (h - r) : 0;
        if (row < r || row >= h - r) {
            while (inset < r) {
                const int dx = r - inset;
                if (dx * dx + dy * dy <= r * r) { break; }
                ++inset;
            }
        }
        fill_rect_physical(canvas, x0 + inset, y0 + row,
                           w - inset * 2, 1, color);
    }
}

static void fill_ellipse(face_canvas_t *canvas, float x0, float y0,
                         float x1, float y1, color_t color)
{
    const float cx = (x0 + x1) * 0.5f;
    const float cy = (y0 + y1) * 0.5f;
    const float rx = (x1 - x0) * 0.5f;
    const float ry = (y1 - y0) * 0.5f;
    if (rx <= 0.0f || ry <= 0.0f) { return; }
    for (int y = (int)y0; y <= (int)y1; ++y) {
        for (int x = (int)x0; x <= (int)x1; ++x) {
            const float nx = (x - cx) / rx;
            const float ny = (y - cy) / ry;
            if (nx * nx + ny * ny <= 1.0f) {
                fill_rect(canvas, (float)x, (float)y, 1.2f, 1.2f, color);
            }
        }
    }
}

static void draw_line(face_canvas_t *canvas, float x0f, float y0f,
                      float x1f, float y1f, int thickness, color_t color)
{
    int x0 = (int)x0f;
    int y0 = (int)y0f;
    const int x1 = (int)x1f;
    const int y1 = (int)y1f;
    const int dx = abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        fill_rect(canvas, (float)x0, (float)y0,
                  (float)thickness, (float)thickness, color);
        if (x0 == x1 && y0 == y1) { break; }
        const int doubled = 2 * error;
        if (doubled >= dy) { error += dy; x0 += sx; }
        if (doubled <= dx) { error += dx; y0 += sy; }
    }
}

static float smoothstep(float value)
{
    if (value < 0.0f) { value = 0.0f; }
    if (value > 1.0f) { value = 1.0f; }
    return value * value * (3.0f - 2.0f * value);
}

static float deterministic_rand(int a, int b, int c)
{
    float value = sinf(a * 12.9898f + b * 78.233f + c * 37.719f) *
                  43758.5453f;
    value -= floorf(value);
    return value < 0.0f ? value + 1.0f : value;
}

static bool smoking_drag(float now, float *progress)
{
    const int window = (int)(now / 10.0f);
    const float local = fmodf(now, 10.0f);
    if (deterministic_rand(window, 0, 0) < 0.4f && local < 4.2f) {
        *progress = local / 4.2f;
        return true;
    }
    return false;
}

static void apply_smoking_pose(float now, float *gaze_x, float *gaze_y,
                               float *height_multiplier)
{
    float progress = 0.0f;
    const float drift_x = sinf(now * 0.5f) * 3.5f;
    const float drift_y = -2.0f + sinf(now * 0.31f) * 2.0f;
    if (smoking_drag(now, &progress)) {
        if (progress < 0.46f) {
            *gaze_x = drift_x * 0.4f;
            *gaze_y = drift_y + 2.0f;
            *height_multiplier = 0.8f;
        } else if (progress < 0.82f) {
            *gaze_x = drift_x * 0.5f;
            *gaze_y = drift_y - 6.0f *
                smoothstep((progress - 0.46f) / (0.82f - 0.46f));
        } else {
            *gaze_x = drift_x;
            *gaze_y = drift_y;
        }
        return;
    }
    const int slot = (int)(now / 3.4f);
    float gx = 0.0f;
    float gy = 0.0f;
    if (deterministic_rand(slot + 1, 0, 0) > 0.35f) {
        gx = (deterministic_rand(slot + 1, 1, 0) * 2.0f - 1.0f) * 14.0f;
        gy = (deterministic_rand(slot + 1, 2, 0) * 2.0f - 1.0f) * 6.0f;
    }
    *gaze_x = drift_x + gx;
    *gaze_y = drift_y + gy;
}

static void fill_polygon(face_canvas_t *canvas, const float pts[][2],
                         int count, color_t color);

/* Upstream painters.lids: flat top/bottom lids carved out of the eye. */
static void paint_lids(face_canvas_t *canvas, float x, float y,
                       float w, float h, float top, float bottom,
                       color_t background)
{
    if (top > 0.0f) {
        fill_rect(canvas, x - 1.0f, y - 1.0f, w + 2.0f, h * top + 1.0f,
                  background);
    }
    if (bottom < 1.0f) {
        fill_rect(canvas, x - 1.0f, y + h * bottom, w + 2.0f,
                  h * (1.0f - bottom) + 1.0f, background);
    }
}

/* Upstream painters.glare: inner-down brow triangle (tip drops toward the nose). */
static void paint_glare(face_canvas_t *canvas, float x, float y,
                        float w, float h, float depth, bool is_right,
                        color_t background)
{
    const float tip_x = is_right ? x - 2.0f : x + w + 2.0f;
    const float tri[3][2] = {
        {x - 2.0f, y - 2.0f}, {x + w + 2.0f, y - 2.0f},
        {tip_x, y + h * depth},
    };
    fill_polygon(canvas, tri, 3, background);
}

/* Upstream painters.brow: slanted top lid (inner toward nose, outer outside). */
static void paint_brow(face_canvas_t *canvas, float x, float y,
                       float w, float h, float inner, float outer,
                       bool is_right, color_t background)
{
    const float rt = y + h * (is_right ? outer : inner);
    const float lt = y + h * (is_right ? inner : outer);
    const float quad[4][2] = {
        {x - 2.0f, y - 2.0f}, {x + w + 2.0f, y - 2.0f},
        {x + w + 2.0f, rt}, {x - 2.0f, lt},
    };
    fill_polygon(canvas, quad, 4, background);
}

static void draw_eye(face_canvas_t *canvas, float center_x, float center_y,
                     float width, float height, bool is_right,
                     desk_expression_t expression,
                     copet_behavior_id_t behavior_id,
                     color_t eye_color, color_t background)
{
    const float x = center_x - width * 0.5f;
    const float y = center_y - height * 0.5f;
    fill_round_rect(canvas, x, y, width, height,
                    fminf(12.0f, fminf(width, height) / 2.0f), eye_color);

    if (expression == DESK_EXPRESSION_HAPPY) {
        fill_ellipse(canvas, x - width * 0.55f, y + height * 0.5f,
                     x + width * 1.55f, y + height * 2.1f, background);
    } else if (expression == DESK_EXPRESSION_BORED) {
        fill_rect(canvas, x - 1.0f, y - 1.0f,
                  width + 2.0f, height * 0.5f, background);
    } else if (expression == DESK_EXPRESSION_CHILL) {
        fill_rect(canvas, x - 1.0f, y - 1.0f,
                  width + 2.0f, height * 0.45f, background);
    } else if (expression == DESK_EXPRESSION_TIRED) {
        /* Upstream tired: brow(0.38, 0.52) -- hooded, peering out. */
        paint_brow(canvas, x, y, width, height, 0.38f, 0.52f, is_right,
                   background);
    }

    if (behavior_id == COPET_BEHAVIOR_ATTENTIVE ||
        behavior_id == COPET_BEHAVIOR_CONNECTING ||
        behavior_id == COPET_BEHAVIOR_LISTENING) {
        paint_lids(canvas, x, y, width, height, 0.12f, 1.0f, background);
    } else if (behavior_id == COPET_BEHAVIOR_FOCUSED) {
        paint_lids(canvas, x, y, width, height, 0.24f, 0.76f, background);
    } else if (behavior_id == COPET_BEHAVIOR_ANGRY) {
        paint_glare(canvas, x, y, width, height, 0.60f, is_right, background);
    } else if (behavior_id == COPET_BEHAVIOR_NERVOUS) {
        paint_brow(canvas, x, y, width, height, 0.02f, 0.26f, is_right,
                   background);
    } else if (behavior_id == COPET_BEHAVIOR_CAT) {
        /* Upstream cat paint: slanted top lid, almond slit pupil, catchlight. */
        paint_brow(canvas, x, y, width, height, 0.14f, 0.0f, is_right,
                   background);
        const float cx = x + width * 0.5f;
        const float top = y + height * 0.24f;
        const float bot = y + height * 0.9f;
        const float midy = (top + bot) * 0.5f;
        float half = width * 0.105f;
        if (half < 2.0f) { half = 2.0f; }
        const float pupil[4][2] = {
            {cx, top}, {cx + half, midy}, {cx, bot}, {cx - half, midy},
        };
        fill_polygon(canvas, pupil, 4, background);
        const float glint = top + (bot - top) * 0.36f;
        fill_ellipse(canvas, cx - 2.0f, glint - 2.0f,
                     cx + 2.0f, glint + 2.0f, eye_color);
    }
}

/* Compact 3x5 digits for the d20 number shown while the die rolls. */
static const uint8_t DIGIT_GLYPHS[10][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 3, 1, 7},
    {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 2, 2, 2},
    {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
};

static void draw_digit(face_canvas_t *canvas, float x, float y, int digit,
                       color_t color)
{
    if (digit < 0 || digit > 9) { return; }
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 3; ++col) {
            if ((DIGIT_GLYPHS[digit][row] & (1U << (2 - col))) != 0U) {
                fill_rect(canvas, x + (float)col, y + (float)row, 1.0f, 1.0f,
                          color);
            }
        }
    }
}

/* Draw a 1..2 digit number centred at (cx, cy). */
static void draw_number(face_canvas_t *canvas, float cx, float cy, int value,
                        color_t color)
{
    const int tens = value / 10;
    const int ones = value % 10;
    const float width = tens > 0 ? 7.0f : 3.0f;   /* 3px/digit + 1px gap */
    float x = cx - width * 0.5f;
    const float y = cy - 2.5f;
    if (tens > 0) {
        draw_digit(canvas, x, y, tens, color);
        x += 4.0f;
    }
    draw_digit(canvas, x, y, ones, color);
}

static void draw_z(face_canvas_t *canvas, float x, float y,
                   float size, color_t color)
{
    draw_line(canvas, x, y, x + size, y, 1, color);
    draw_line(canvas, x + size, y, x, y + size, 1, color);
    draw_line(canvas, x, y + size, x + size, y + size, 1, color);
}

/* Scanline fill of a closed polygon given in logical face coordinates. */
static void fill_polygon(face_canvas_t *canvas, const float pts[][2],
                         int count, color_t color)
{
    float min_y = pts[0][1];
    float max_y = pts[0][1];
    for (int i = 1; i < count; ++i) {
        if (pts[i][1] < min_y) { min_y = pts[i][1]; }
        if (pts[i][1] > max_y) { max_y = pts[i][1]; }
    }
    for (int y = (int)floorf(min_y); y <= (int)ceilf(max_y); ++y) {
        const float scan = (float)y + 0.5f;
        float crossings[16];
        int found = 0;
        for (int i = 0; i < count && found < 16; ++i) {
            const int j = (i + 1) % count;
            const float ya = pts[i][1];
            const float yb = pts[j][1];
            if ((ya <= scan && yb > scan) || (yb <= scan && ya > scan)) {
                const float t = (scan - ya) / (yb - ya);
                crossings[found++] = pts[i][0] + t * (pts[j][0] - pts[i][0]);
            }
        }
        for (int a = 0; a < found - 1; ++a) {
            for (int b = a + 1; b < found; ++b) {
                if (crossings[b] < crossings[a]) {
                    const float tmp = crossings[a];
                    crossings[a] = crossings[b];
                    crossings[b] = tmp;
                }
            }
        }
        for (int a = 0; a + 1 < found; a += 2) {
            fill_rect(canvas, crossings[a], (float)y,
                      crossings[a + 1] - crossings[a], 1.0f, color);
        }
    }
}

/* Upstream painters.sparkle: a 4-point twinkle star. */
static void draw_sparkle(face_canvas_t *canvas, float cx, float cy,
                         float s, color_t color)
{
    const float outer = s;
    const float inner = s * 0.34f;
    float pts[8][2];
    for (int k = 0; k < 8; ++k) {
        const float radius = (k % 2 == 0) ? outer : inner;
        const float angle = -1.57079633f + (float)k * 0.78539816f;
        pts[k][0] = cx + radius * cosf(angle);
        pts[k][1] = cy + radius * sinf(angle);
    }
    fill_polygon(canvas, pts, 8, color);
}

/* Upstream lovely._heart: smooth parametric heart ~s px wide, centred at (cx,cy). */
static void draw_heart(face_canvas_t *canvas, float cx, float cy,
                       float s, color_t color)
{
    const float sc = s / 33.0f;
    float pts[72][2];
    for (int i = 0; i < 72; ++i) {
        const float t = (float)i * 3.14159265f / 36.0f;
        const float st = sinf(t);
        pts[i][0] = cx + 16.0f * st * st * st * sc;
        pts[i][1] = cy - (13.0f * cosf(t) - 5.0f * cosf(2.0f * t) -
                          2.0f * cosf(3.0f * t) - cosf(4.0f * t)) * sc +
                    2.0f * sc;
    }
    fill_polygon(canvas, pts, 72, color);
}

/* Upstream lovely._beat: lub-dub, ~1 beat/sec. */
static float heart_beat(float now)
{
    const float p = fmodf(now * 1.1f, 1.0f);
    const float a = p * 7.0f;
    const float b = (p - 0.24f) * 7.0f;
    return expf(-a * a) + 0.55f * expf(-b * b);
}

/* Upstream cat._ear: a slim open V that twitches outward now and then. */
static void draw_cat_ear(face_canvas_t *canvas, float ax, float ay,
                         float bx, float by, float tip_x, float tip_y,
                         float now, float side, color_t color)
{
    const float phase = fmodf(now / 4.0f + (side < 0.0f ? 0.37f : 0.61f), 1.0f);
    const float flick = phase < 0.09f
        ? sinf(phase / 0.09f * 3.14159265f)
        : 0.0f;
    const float tx = tip_x + flick * 3.0f * side;
    const float ty = tip_y - flick * 1.5f;
    draw_line(canvas, ax, ay, tx, ty, 2, color);
    draw_line(canvas, tx, ty, bx, by, 2, color);
}

/* PIL-style elliptical arc from start_deg to end_deg (y down), thick lines. */
static void draw_arc(face_canvas_t *canvas, float x0, float y0,
                     float x1, float y1, int start_deg, int end_deg,
                     int thickness, color_t color)
{
    const float cx = (x0 + x1) * 0.5f;
    const float cy = (y0 + y1) * 0.5f;
    const float rx = (x1 - x0) * 0.5f;
    const float ry = (y1 - y0) * 0.5f;
    const float t = (float)thickness;
    float px = cx + rx * cosf((float)start_deg * 3.14159265f / 180.0f);
    float py = cy + ry * sinf((float)start_deg * 3.14159265f / 180.0f);
    for (int a = start_deg + 15; a <= end_deg; a += 15) {
        const float angle = (float)a * 3.14159265f / 180.0f;
        const float x = cx + rx * cosf(angle);
        const float y = cy + ry * sinf(angle);
        draw_line(canvas, px, py, x, y, thickness, color);
        px = x;
        py = y;
    }
    (void)t;
}

/* Eyelid openness over a blink, 1->0->1 x reps (upstream primitives.lid_openness). */
static float lid_openness(float u, int reps)
{
    const float close = 0.34f;
    const float clamped = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
    const float seg = fmodf(clamped * (float)reps, 1.0f);
    if (seg < close) {
        return 1.0f - smoothstep(seg / close);
    }
    return smoothstep((seg - close) / (1.0f - close));
}

/* ---- Faithful port of the upstream sleepy nap timeline (moods/sleepy.py) ---- */
static void sleepy_gaze(float now, float *gx, float *gy)
{
    const float glance = 2.2f;
    const float g = now / glance;
    const int i = (int)g;
    float tx[2];
    float ty[2];
    for (int k = 0; k < 2; ++k) {
        const int n = i + k;
        if (deterministic_rand(n, 7, 0) < 0.3f) {
            tx[k] = 0.0f;
            ty[k] = 0.0f;
        } else {
            tx[k] = (deterministic_rand(n, 0, 0) * 2.0f - 1.0f) * 15.0f;
            ty[k] = (deterministic_rand(n, 1, 0) * 2.0f - 1.0f) * 6.0f;
        }
    }
    const float e = smoothstep(fminf(1.0f, (g - (float)i) / 0.35f));
    *gx = tx[0] + (tx[1] - tx[0]) * e;
    *gy = ty[0] + (ty[1] - ty[0]) * e;
}

static float sleepy_blink(float now)
{
    const float g = now / 2.2f;
    const int gi = (int)g;
    const float f = g - (float)gi;
    if (f > 0.18f || deterministic_rand(gi, 3, 0) > 0.4f) {
        return 0.0f;
    }
    return sinf(f / 0.18f * 3.14159265f);
}

/* (visible eye height, gaze x, gaze y, zzz phase or -1) for the instant `now`. */
static void sleepy_state(float now, float *vis, float *sx, float *sy,
                         float *zzz)
{
    const float look = 5.0f;
    const float close = 0.9f;
    const float hold = 1.4f;
    const float shudder = 0.35f;
    const float settle = 0.9f;
    const float period = look + close + hold + shudder + settle;   /* 8.55 */
    const float asleep = 1.0f;
    const float squint = 11.0f;
    const float wake = 18.0f;

    const float window_start = floorf(now / period) * period;
    float t = now - window_start;
    float lx;
    float ly;
    sleepy_gaze(now, &lx, &ly);
    const float bob = sinf(now * 1.7f);
    *zzz = -1.0f;

    if (t < look ||
        deterministic_rand((int)(now / period), 0, 0) >= 0.5f) {
        *vis = squint + (asleep - squint) * sleepy_blink(now);
        *sx = lx;
        *sy = ly + bob;
        return;
    }
    float ax;
    float ay;
    sleepy_gaze(window_start + look, &ax, &ay);
    t -= look;
    if (t < close) {
        const float e = smoothstep(t / close);
        *vis = squint + (asleep - squint) * e;
        *sx = ax;
        *sy = ay + bob * (1.0f - e);
        return;
    }
    t -= close;
    if (t < hold) {
        *vis = asleep;
        *sx = ax;
        *sy = ay;
        *zzz = t / hold;
        return;
    }
    t -= hold;
    if (t < shudder) {
        const float f = t / shudder;
        const float tr = 1.0f - f;
        *vis = asleep + (wake - asleep) * smoothstep(fminf(1.0f, f / 0.5f));
        *sx = ax + tr * 3.0f * sinf(now * 75.0f);
        *sy = ay + tr * 1.5f * sinf(now * 90.0f + 1.0f);
        return;
    }
    const float e = smoothstep((t - shudder) / settle);
    *vis = wake + (squint - wake) * e;
    *sx = ax + (lx - ax) * e;
    *sy = ay + (ly + bob - ay) * e;
}

static void draw_sleepy_eye(face_canvas_t *canvas, float cx, float cy,
                            float h, color_t color)
{
    const float w = 34.0f;
    const float r = fminf(w, h) * 12.0f / 36.0f;
    fill_round_rect(canvas, cx - w * 0.5f, cy - h * 0.5f, w, h, r, color);
}

static void draw_sleepy(face_canvas_t *canvas, float now, color_t color)
{
    float vis;
    float sx;
    float sy;
    float zzz;
    sleepy_state(now, &vis, &sx, &sy, &zzz);
    draw_sleepy_eye(canvas, 41.0f + sx, 32.0f + sy, vis, color);
    draw_sleepy_eye(canvas, 87.0f + sx, 32.0f + sy, vis, color);
    if (zzz >= 0.0f) {
        for (int i = 0; i < 3; ++i) {
            const float a = zzz * 3.0f - (float)i;
            if (a >= 0.0f) {
                const float zx = 87.0f + sx + 12.0f + (float)i * 5.0f;
                const float zy = 32.0f - 6.0f - (float)i * 6.0f -
                                 fminf(1.0f, a) * 6.0f;
                draw_z(canvas, zx, zy, 4.0f + (float)i * 2.0f, color);
            }
        }
    }
}

/* ---- Faithful port of the upstream zen leaves (vibes/zen.py) ---- */
static const float ZEN_WIND_A[3] = {1.8f, 0.9f, 0.5f};
static const float ZEN_WIND_W[3] = {0.10f, 0.29f, 0.73f};
static const float ZEN_WIND_P[3] = {0.0f, 1.7f, 4.2f};

static float zen_push(float t0, float t1)
{
    float s = 6.5f * (t1 - t0);
    for (int k = 0; k < 3; ++k) {
        s += ZEN_WIND_A[k] *
             (cosf(ZEN_WIND_W[k] * t0 + ZEN_WIND_P[k]) -
              cosf(ZEN_WIND_W[k] * t1 + ZEN_WIND_P[k])) / ZEN_WIND_W[k];
    }
    return s;
}

static void draw_zen_leaves(face_canvas_t *canvas, float now, color_t color)
{
    static const float shape[10][2] = {
        {0.0f, 1.05f}, {-0.5f, 0.4f}, {-0.68f, -0.3f}, {-0.5f, -0.82f},
        {-0.22f, -0.98f}, {0.0f, -0.46f}, {0.22f, -0.98f}, {0.5f, -0.82f},
        {0.68f, -0.3f}, {0.5f, 0.4f},
    };
    float windv = 6.5f;
    for (int k = 0; k < 3; ++k) {
        windv += ZEN_WIND_A[k] * sinf(ZEN_WIND_W[k] * now + ZEN_WIND_P[k]);
    }
    for (int i = 0; i < 9; ++i) {
        const float period = 14.0f + (float)(i % 4) * 2.0f;
        const float u = now / period + (float)i * 0.41f;
        const int cyc = (int)u;
        const float el = (u - (float)cyc) * period;
        const int base = i * 9 + cyc * 131;
#define ZEN_R(n) deterministic_rand(base, (n), 0)
        const float m = ZEN_R(0);
        const float vfall = 6.0f + m * 10.0f;
        const float sail = 0.5f + (1.0f - m) * 1.7f + ZEN_R(1) * 0.7f;
        const float lag = m * 0.6f + ZEN_R(2) * 0.6f;
        const float fph = el * (2.0f + (1.0f - m) * 3.0f) + ZEN_R(3) * 6.0f;
        const float x = -10.0f + ZEN_R(4) * 128.0f * 0.35f +
                        sail * zen_push(now - lag - el, now - lag) +
                        sinf(fph) * (1.8f + (1.0f - m) * 3.2f) *
                            (0.6f + windv * 0.07f);
        const float y = -7.0f + ZEN_R(5) * 5.0f + vfall * el +
                        sinf(fph * 0.5f) * 1.6f * (1.0f - m);
        if (x > 137.0f || y > 73.0f) {
            continue;
        }
        const float ang = el * (0.5f + (1.0f - m) * 1.3f) *
                              (ZEN_R(6) > 0.5f ? 1.0f : -1.0f) +
                          cosf(fph) * 0.6f + ZEN_R(7) * 6.0f;
        const float fore = 0.4f + 0.6f *
            fabsf(cosf(el * (0.7f + (1.0f - m)) + ZEN_R(8) * 6.0f));
        const float wid = (2.4f + ZEN_R(1) * 1.8f) * fore;
        const float lng = 3.0f + ZEN_R(2) * 3.0f;
        const float bend = (ZEN_R(6) - 0.5f) * 0.9f;
        const float ca = cosf(ang);
        const float sa = sinf(ang);
        float pts[10][2];
        for (int p = 0; p < 10; ++p) {
            const float bx = (shape[p][0] + bend * shape[p][1] * shape[p][1]) *
                             wid;
            const float by = shape[p][1] * lng;
            pts[p][0] = x + bx * ca - by * sa;
            pts[p][1] = y + bx * sa + by * ca;
        }
        fill_polygon(canvas, pts, 10, color);
#undef ZEN_R
    }
}

static void draw_smoke_curl(face_canvas_t *canvas, float now,
                            float tx, float ty, float scale,
                            color_t smoke_color)
{
    if (scale <= 0.03f) { return; }
    float previous_x = tx;
    float previous_y = ty - 2.0f;
    for (int index = 1; index < 16; ++index) {
        const float f = index / 15.0f;
        const float x = tx + sinf(f * 4.5f - now * 0.9f) *
                        (f * f * 8.0f) * scale;
        const float y = ty - 2.0f - f * (ty - 4.0f) * scale;
        draw_line(canvas, previous_x, previous_y, x, y,
                  1, smoke_color);
        previous_x = x;
        previous_y = y;
    }
}

static void draw_smoking(face_canvas_t *canvas, float now,
                         color_t eye_color, color_t smoke_color)
{
    float p = 0.0f;
    const bool dragging = smoking_drag(now, &p);
    float away = 0.0f;
    float inhale = 0.0f;
    if (dragging && p < 0.18f) {
        inhale = smoothstep(p / 0.18f);
    } else if (dragging && p < 0.38f) {
        away = smoothstep((p - 0.18f) / 0.20f);
    } else if (dragging && p < 0.82f) {
        away = 1.0f;
    } else if (dragging) {
        away = 1.0f - smoothstep((p - 0.82f) / 0.18f);
    }

    const float hx = 74.0f + away * 16.0f;
    const float hy = 54.0f + away * 6.0f;
    const float tx = 95.0f + away * 16.0f;
    const float ty = 57.0f + away * 6.0f;
    draw_line(canvas, hx, hy, tx, ty, 4, eye_color);
    const float glow = 2.0f + 2.0f * inhale + 0.4f * sinf(now * 6.0f);
    fill_ellipse(canvas, tx - glow, ty - glow,
                 tx + glow, ty + glow,
                 rgb332(255, 105, 45));

    if (!dragging) {
        draw_smoke_curl(canvas, now, tx, ty, 1.0f, smoke_color);
    } else if (p < 0.18f) {
        draw_smoke_curl(canvas, now, tx, ty, 1.0f - inhale, smoke_color);
    } else if (p < 0.82f) {
        float previous_x = tx;
        float previous_y = ty - 1.0f;
        for (int index = 1; index < 10; ++index) {
            const float f = index / 9.0f;
            const float x = tx + sinf(f * 5.0f - now * 1.4f) *
                            (1.0f + f * 4.0f);
            const float y = ty - 1.0f - f * 16.0f;
            draw_line(canvas, previous_x, previous_y, x, y,
                      1, smoke_color);
            previous_x = x;
            previous_y = y;
        }
    } else {
        draw_smoke_curl(canvas, now, tx, ty, 1.0f - away, smoke_color);
    }

    if (dragging && p >= 0.46f && p < 0.82f) {
        const float mouth_x = 74.0f;
        const float mouth_y = 54.0f;
        const float exhale_progress = (p - 0.46f) / (0.82f - 0.46f);
        const float rise = smoothstep(exhale_progress) * 1.7f;
        const float fade = exhale_progress < 0.4f
            ? 1.0f
            : smoothstep((1.0f - exhale_progress) / 0.6f);
        for (int index = 0; index < 22; ++index) {
            const float f = index / 21.0f;
            const float front = rise - f * 0.9f;
            if (front <= 0.0f) { continue; }
            const float center_x = mouth_x - f * 6.0f +
                sinf(f * 3.4f + p * 4.0f) * (2.0f + f * 10.0f);
            const float spread = 3.0f + f * 16.0f;
            const float front_scale = fminf(1.0f, front * 2.4f);
            const float base = (2.5f + f * 8.0f) * front_scale * fade;
            for (int puff = -1; puff <= 1; ++puff) {
                const float bx = center_x + puff * spread * 0.5f;
                const float by = mouth_y - f * (mouth_y + 2.0f) -
                                 rise * 3.0f;
                const float radius = base *
                    (1.0f - 0.28f * (puff < 0 ? -puff : puff));
                if (radius > 0.5f) {
                    fill_ellipse(canvas, bx - radius, by - radius,
                                 bx + radius, by + radius, smoke_color);
                }
            }
        }
    }
}

static void draw_heat(face_canvas_t *canvas, float now, color_t color)
{
    for (int index = 0; index < 3; ++index) {
        const float life = 2.6f;
        const float offset = deterministic_rand(index, 1, 0) * life;
        const int cycle = (int)((now + offset) / life);
        const float u = fmodf(now + offset, life) / life;
        if (u > 0.2f + 0.8f *
            deterministic_rand(index, cycle, 7) *
            deterministic_rand(index, cycle, 7)) {
            continue;
        }
        const float y = 63.0f - u * 0.92f * 64.0f *
            (0.8f + 0.4f * deterministic_rand(index, cycle, 5));
        const float x = 4.0f + deterministic_rand(index, cycle, 2) * 120.0f +
            sinf(now * 3.0f + index * 1.7f) * (0.6f + 4.0f * u);
        fill_ellipse(canvas, x - 1.0f, y - 1.0f,
                     x + 1.0f, y + 1.0f, color);
    }
}

typedef struct {
    desk_expression_t expression;
    copet_behavior_id_t behavior_id;
    float gaze_x;
    float gaze_y;
    float eye_width;
    float eye_height;
    float left_height;
    float right_height;
    float left_y_offset;
    float right_y_offset;
    float motion_x;
    float motion_y;
    float tilt;   /* per-eye y-skew: +tilt on the right eye, -tilt on the left */
    float bias;   /* eye-size asymmetry: right eye *(1+bias), left *(1-bias) */
} face_pose_t;

static float behavior_progress(const copet_behavior_view_t *behavior)
{
    if (behavior == NULL || behavior->duration_ms == 0U) { return 0.0f; }
    float progress = behavior->elapsed_ms / (float)behavior->duration_ms;
    if (progress < 0.0f) { return 0.0f; }
    return progress > 1.0f ? 1.0f : progress;
}

/* Layer 1: legacy Desk state supplies the base eyes and autonomous blink. */
static face_pose_t build_base_pose(const desk_mode_view_t *view, float now)
{
    face_pose_t pose = {
        .expression = DESK_EXPRESSION_NEUTRAL,
        .behavior_id = COPET_BEHAVIOR_NEUTRAL,
        .eye_width = 36.0f,
        .eye_height = 36.0f,
    };
    if (view == NULL) {
        pose.left_height = pose.eye_height;
        pose.right_height = pose.eye_height;
        return pose;
    }

    pose.expression = view->expression;
    pose.gaze_x = view->gaze_x;
    pose.gaze_y = view->gaze_y;
    pose.motion_y = view->bob_y * 0.6f;
    float height_multiplier = view->eye_open_percent / 100.0f;
    if (view->expression == DESK_EXPRESSION_SCARED) {
        pose.eye_width = 26.0f;
        pose.eye_height = 32.0f;
    } else if (view->expression == DESK_EXPRESSION_SURPRISED) {
        pose.eye_width = 32.0f;
        pose.eye_height = 46.0f;
    } else if (view->expression == DESK_EXPRESSION_CHILL) {
        pose.eye_height = 32.0f;
    } else if (view->expression == DESK_EXPRESSION_SLEEPY) {
        pose.eye_height = 11.0f;
        const float phase = fmodf(now, 8.55f);
        if (phase > 5.9f && phase < 7.3f) {
            pose.eye_height = 1.2f;
        }
    }

    if (view->vibe == DESK_VIBE_SMOKING) {
        apply_smoking_pose(now, &pose.gaze_x, &pose.gaze_y,
                           &height_multiplier);
    } else if (view->vibe == DESK_VIBE_SHIVER) {
        const float progress = fmodf(view->effect_elapsed_ms / 700.0f, 1.0f);
        const float envelope = 1.0f - progress;
        pose.motion_x += sinf(progress * 3.14159265f * 16.0f) *
                         3.0f * envelope;
        pose.motion_y += cosf(progress * 3.14159265f * 22.0f) *
                         2.0f * envelope;
    } else if (view->vibe == DESK_VIBE_OVERHEATED) {
        pose.motion_x += sinf(now * 8.0f) * 1.4f +
                         sinf(now * 13.0f) * 0.7f;
    }

    if (view->reacting && view->reaction_elapsed_ms < 900U &&
        view->touch_reaction == DESK_TOUCH_EXCITED) {
        const float progress = view->reaction_elapsed_ms / 900.0f;
        const float envelope = 1.0f - progress;
        pose.motion_y -= fabsf(sinf(progress * 3.14159265f * 5.0f)) *
                         8.0f * envelope;
        pose.eye_width *= 1.0f + 0.22f * envelope;
        pose.eye_height *= 1.0f + 0.22f * envelope;
    } else if (view->reacting && view->reaction_elapsed_ms < 900U &&
               view->touch_reaction == DESK_TOUCH_HAPPY) {
        const float progress = view->reaction_elapsed_ms / 900.0f;
        pose.motion_y -= fabsf(sinf(progress * 3.14159265f)) * 3.0f;
    }

    pose.eye_height *= fmaxf(0.08f, height_multiplier);
    pose.left_height = pose.eye_height;
    pose.right_height = pose.eye_height;
    if (view->reacting && view->touch_reaction == DESK_TOUCH_WINK &&
        view->reaction_elapsed_ms < 900U) {
        const float progress = view->reaction_elapsed_ms / 900.0f;
        float openness;
        if (progress < 0.25f) {
            openness = 1.0f - smoothstep(progress / 0.25f);
        } else if (progress < 0.55f) {
            openness = 0.08f;
        } else {
            openness = smoothstep((progress - 0.55f) / 0.45f);
        }
        pose.left_height *= fmaxf(0.08f, openness);
    }
    return pose;
}

/* Layer 2: a behavior may replace only the mood/base geometry. */
static void apply_behavior_mood_layer(
    face_pose_t *pose, const copet_behavior_view_t *behavior)
{
    if (behavior == NULL) { return; }
    pose->behavior_id = behavior->id;
    const float elapsed_s = behavior->elapsed_ms / 1000.0f;
    bool geometry_changed = true;
    switch (behavior->id) {
    case COPET_BEHAVIOR_ALERT:
        /* Upstream: dw=-18 -> two upright bars. */
        pose->eye_width = 18.0f;
        pose->eye_height = 36.0f;
        pose->gaze_x = 0.0f;
        pose->gaze_y = 0.0f;
        break;
    case COPET_BEHAVIOR_ATTENTIVE:
        /* Upstream: dw=2, dh=2, lids(top=0.12). */
        pose->eye_width = 38.0f;
        pose->eye_height = 38.0f;
        pose->gaze_x = 0.0f;
        pose->gaze_y = 0.0f;
        break;
    case COPET_BEHAVIOR_FOCUSED:
        /* Upstream: base eyes, lids(0.24, 0.76) -> determined band. */
        pose->eye_width = 36.0f;
        pose->eye_height = 36.0f;
        pose->gaze_x = 0.0f;
        pose->gaze_y = 0.0f;
        break;
    case COPET_BEHAVIOR_ANGRY:
        /* Upstream: base eyes, glare(0.60) -> inner-down brow. */
        pose->eye_width = 36.0f;
        pose->eye_height = 36.0f;
        pose->gaze_x = 0.0f;
        pose->gaze_y = 0.0f;
        break;
    case COPET_BEHAVIOR_DISORIENTED:
        /* Upstream: tilt=4, bias=0.3, sway=(3.0, 2.2) -> woozy seesaw. */
        pose->eye_width = 36.0f;
        pose->eye_height = 36.0f;
        pose->gaze_x = 0.0f;
        pose->gaze_y = 0.0f;
        pose->tilt = 4.0f + 3.0f * sinf(elapsed_s * 2.2f);
        pose->bias = 0.3f;
        break;
    case COPET_BEHAVIOR_CONNECTING:
        /* Upstream action: attentive face + expectant gaze + link dots. */
        pose->eye_width = 38.0f;
        pose->eye_height = 38.0f;
        pose->gaze_x = sinf(elapsed_s * 1.5f) * 3.0f;
        pose->gaze_y = sinf(elapsed_s * 2.0f) * 2.0f;
        break;
    case COPET_BEHAVIOR_LISTENING:
        /* Upstream action: attentive face, gently nodding under headphones. */
        pose->eye_width = 38.0f;
        pose->eye_height = 38.0f;
        pose->gaze_x = sinf(elapsed_s * 1.8f) * 2.0f;
        pose->gaze_y = sinf(elapsed_s * 3.6f) * 2.0f;
        break;
    case COPET_BEHAVIOR_ZEN:
        /* Upstream vibe: neutral eyes squished to 12% height (calm slits). */
        pose->eye_width = 36.0f;
        pose->eye_height = 36.0f * 0.12f;
        pose->gaze_x = 0.0f;
        pose->gaze_y = 0.0f;
        break;
    case COPET_BEHAVIOR_DICE_ROLL:
        /* Upstream vibe: small eyes, low, looking up at the die. */
        pose->eye_width = 36.0f;
        pose->eye_height = 36.0f * 0.55f;
        pose->gaze_x = 0.0f;
        pose->gaze_y = 9.0f;
        break;
    case COPET_BEHAVIOR_HAPPY:
        /* Upstream: base eyes, cheeks-up smile carve (DESK_EXPRESSION_HAPPY). */
        pose->expression = DESK_EXPRESSION_HAPPY;
        pose->eye_width = 36.0f;
        pose->eye_height = 36.0f;
        pose->gaze_x = 0.0f;
        pose->gaze_y = 0.0f;
        break;
    case COPET_BEHAVIOR_KAWAII:
        /* Round base eyes, a touch wider (upstream dw=2); the rest is decor. */
        pose->eye_width = 38.0f;
        pose->eye_height = 36.0f;
        pose->gaze_x = 0.0f;
        pose->gaze_y = 0.0f;
        break;
    case COPET_BEHAVIOR_CHILL:
        /* Upstream: dh=-4, lids(top=0.45) -> heavy-lidded (DESK CHILL carve). */
        pose->expression = DESK_EXPRESSION_CHILL;
        pose->eye_width = 36.0f;
        pose->eye_height = 32.0f;
        pose->gaze_x = 0.0f;
        pose->gaze_y = 0.0f;
        break;
    case COPET_BEHAVIOR_NERVOUS:
        /* Upstream: dw=-2, brow(0.02,0.26) + a sliding sweat bead (decor). */
        pose->eye_width = 34.0f;
        pose->eye_height = 36.0f;
        pose->gaze_x = 0.0f;
        pose->gaze_y = 0.0f;
        break;
    case COPET_BEHAVIOR_CAT:
        /* Smaller eyes (upstream dw=-6, dh=-8) to leave room for the ears. */
        pose->eye_width = 30.0f;
        pose->eye_height = 28.0f;
        pose->gaze_x = 0.0f;
        pose->gaze_y = 0.0f;
        break;
    case COPET_BEHAVIOR_LOVELY:
        /* Eyes are replaced by beating hearts in render_face_layers. */
        pose->eye_width = 36.0f;
        pose->eye_height = 36.0f;
        pose->gaze_x = 0.0f;
        pose->gaze_y = 0.0f;
        break;
    case COPET_BEHAVIOR_LEGACY_SCARED:
        pose->expression = DESK_EXPRESSION_SCARED;
        pose->eye_width = 26.0f;
        pose->eye_height = 32.0f;
        break;
    case COPET_BEHAVIOR_NEUTRAL:
    case COPET_BEHAVIOR_DOUBLE_BLINK:
    case COPET_BEHAVIOR_LOOK_LEFT:
    case COPET_BEHAVIOR_LOOK_RIGHT:
    case COPET_BEHAVIOR_ACKNOWLEDGE:
    case COPET_BEHAVIOR_ID_COUNT:
    default:
        geometry_changed = false;
        break;
    }
    if (geometry_changed) {
        pose->left_height = pose->eye_height;
        pose->right_height = pose->eye_height;
    }
}

/* Layer 3: gestures modify openness/gaze without replacing the mood. */
static void apply_behavior_gesture_layer(
    face_pose_t *pose, const copet_behavior_view_t *behavior)
{
    if (behavior == NULL) { return; }
    if (behavior->id == COPET_BEHAVIOR_DOUBLE_BLINK) {
        /* Upstream: blink({left,right}, reps=2) -> lid_openness(p, 2). */
        const float openness = lid_openness(behavior_progress(behavior), 2);
        pose->left_height *= openness;
        pose->right_height *= openness;
    } else if (behavior->id == COPET_BEHAVIOR_LOOK_LEFT ||
               behavior->id == COPET_BEHAVIOR_LOOK_RIGHT) {
        /* Upstream look(dx,0,bias): dart out, parallax-swell the near eye. */
        const float p = behavior_progress(behavior);
        float hold;
        if (p < 0.22f) {
            hold = p / 0.22f;
        } else if (p > 0.80f) {
            hold = (1.0f - p) / 0.20f;
        } else {
            hold = 1.0f;
        }
        hold = smoothstep(hold);
        const bool left = behavior->id == COPET_BEHAVIOR_LOOK_LEFT;
        pose->gaze_x += (left ? -8.0f : 8.0f) * hold;
        pose->bias += (left ? -0.2f : 0.2f) * hold;
        const float s = 1.0f - 0.12f * hold;
        pose->eye_width *= s;
        pose->left_height *= s;
        pose->right_height *= s;
    }
}

/* Layer 4: reactions move the complete eye group after shape selection. */
static void apply_behavior_transform_layer(
    face_pose_t *pose, const copet_behavior_view_t *behavior)
{
    if (behavior == NULL) { return; }
    if (behavior->id == COPET_BEHAVIOR_ACKNOWLEDGE) {
        /* Upstream reaction: one crisp dip, dy = sin(p*pi) * 8. */
        const float e = sinf(behavior_progress(behavior) * 3.14159265f);
        pose->motion_y += e * 8.0f;
    }
}

/* Layer 5: action overlays are drawn after the eyes, before screen HUD. */
static void draw_behavior_overlay_layer(
    face_canvas_t *canvas, const copet_behavior_view_t *behavior,
    color_t eye_color)
{
    if (behavior == NULL) { return; }
    if (behavior->id == COPET_BEHAVIOR_NERVOUS) {
        /* Upstream nervous decor: a bead wells up by the brow, then slides. */
        const float t = fmodf(behavior->elapsed_ms / 1000.0f * 0.8f, 1.0f);
        const float x = 128.0f - 16.0f;
        const float y = 8.0f + t * 11.0f;
        const float s = 3.0f;
        fill_ellipse(canvas, x - s * 0.7f, y - s * 0.3f,
                     x + s * 0.7f, y + s, eye_color);
        const float tip[3][2] = {
            {x, y - s - 3.0f}, {x - s * 0.6f, y - s * 0.2f},
            {x + s * 0.6f, y - s * 0.2f},
        };
        fill_polygon(canvas, tip, 3, eye_color);
    } else if (behavior->id == COPET_BEHAVIOR_CONNECTING) {
        /* Upstream: three link dots pulse in sequence near the bottom. */
        const float now = behavior->elapsed_ms / 1000.0f;
        const float cy = 64.0f - 11.0f;
        for (int i = 0; i < 3; ++i) {
            const float t = (sinf(now * 4.0f - (float)i * 1.1f) + 1.0f) * 0.5f;
            const float s = 1.5f + 2.5f * t;
            const float x = 64.0f - 10.0f + (float)i * 10.0f;
            fill_ellipse(canvas, x - s * 0.5f, cy - s * 0.5f,
                         x + s * 0.5f, cy + s * 0.5f, eye_color);
        }
    } else if (behavior->id == COPET_BEHAVIOR_LISTENING) {
        /* Upstream: a headband arc + an ear cup on each side. */
        const float cw = 11.0f;
        const float ch = 22.0f;
        const float cy = 64.0f / 2.0f - ch / 2.0f;
        fill_round_rect(canvas, 2.0f, cy, cw, ch, 4.0f, eye_color);
        fill_round_rect(canvas, 128.0f - 3.0f - cw, cy, cw, ch, 4.0f,
                        eye_color);
        draw_arc(canvas, 8.0f, 1.0f, 128.0f - 9.0f, 64.0f - 12.0f,
                 180, 360, 3, eye_color);
    } else if (behavior->id == COPET_BEHAVIOR_ZEN) {
        draw_zen_leaves(canvas, behavior->elapsed_ms / 1000.0f, eye_color);
    } else if (behavior->id == COPET_BEHAVIOR_DICE_ROLL) {
        /* Upstream vibe: a d20 hexagon tumbles ~1.4s then holds a number. */
        const float t = behavior->elapsed_ms / 1000.0f;
        const float cx = 64.0f;
        const float cy = 12.0f;
        float jx = 0.0f;
        float jy = 0.0f;
        int value;
        if (t < 1.4f) {
            value = 1 + (int)(deterministic_rand((int)(t * 18.0f), 0, 0) * 20.0f);
            jx = (deterministic_rand((int)(t * 18.0f), 1, 0) - 0.5f) * 3.0f;
            jy = (deterministic_rand((int)(t * 18.0f), 2, 0) - 0.5f) * 3.0f;
        } else {
            value = 1 + (int)(deterministic_rand(7, 3, 1) * 20.0f);
        }
        if (value > 20) { value = 20; }
        float hex[6][2];
        for (int k = 0; k < 6; ++k) {
            const float a = 3.14159265f / 6.0f + (float)k * 3.14159265f / 3.0f;
            hex[k][0] = cx + jx + 10.0f * cosf(a);
            hex[k][1] = cy + jy + 10.0f * sinf(a);
        }
        for (int k = 0; k < 6; ++k) {
            draw_line(canvas, hex[k][0], hex[k][1], hex[(k + 1) % 6][0],
                      hex[(k + 1) % 6][1], 1, eye_color);
        }
        draw_number(canvas, cx + jx, cy + jy, value, eye_color);
    } else if (behavior->id == COPET_BEHAVIOR_KAWAII) {
        /* Rosy blush hatch under each eye + three ambient twinkles. */
        const float cxs[2] = {14.0f, 128.0f - 24.0f};
        for (int side = 0; side < 2; ++side) {
            for (int i = 0; i < 3; ++i) {
                const float x = cxs[side] + (float)i * 4.0f;
                draw_line(canvas, x, 64.0f - 12.0f, x + 3.0f, 64.0f - 7.0f,
                          1, eye_color);
            }
        }
        draw_sparkle(canvas, 0.07f * 128.0f, 0.16f * 64.0f, 4.0f, eye_color);
        draw_sparkle(canvas, 0.93f * 128.0f, 0.18f * 64.0f, 4.0f, eye_color);
        draw_sparkle(canvas, 0.50f * 128.0f, 0.06f * 64.0f, 3.0f, eye_color);
    } else if (behavior->id == COPET_BEHAVIOR_LOVELY) {
        /* A couple of little hearts drift up, plus a sparkle on each side. */
        const float now = behavior->elapsed_ms / 1000.0f;
        for (int i = 0; i < 2; ++i) {
            const float t = fmodf(now * 0.3f + (float)i * 0.5f, 1.0f);
            const float fx = (i == 0 ? 0.12f : 0.88f) +
                             0.03f * sinf(now * 2.0f + (float)i);
            draw_heart(canvas, fx * 128.0f, 64.0f * (1.0f - t) - 2.0f,
                       4.0f + 2.0f * (1.0f - t), eye_color);
        }
        for (int i = 0; i < 2; ++i) {
            const float fx = i == 0 ? 0.07f : 0.93f;
            const float fy = i == 0 ? 0.22f : 0.24f;
            draw_sparkle(canvas, fx * 128.0f, fy * 64.0f,
                         1.5f + 2.0f * fabsf(sinf(now * 3.0f + (float)i * 1.6f)),
                         eye_color);
        }
    } else if (behavior->id == COPET_BEHAVIOR_CAT) {
        /* Ears, whiskers and a tiny :3 nose+mouth ride above/below the eyes. */
        const float now = behavior->elapsed_ms / 1000.0f;
        draw_cat_ear(canvas, 28.0f, 15.0f, 45.0f, 12.0f, 35.0f, 1.0f,
                     now, -1.0f, eye_color);
        draw_cat_ear(canvas, 128.0f - 28.0f, 15.0f, 128.0f - 45.0f, 12.0f,
                     128.0f - 35.0f, 1.0f, now, 1.0f, eye_color);
        for (int i = 0; i < 3; ++i) {
            const float dy = (float)(i - 1) * 6.0f;
            const float wob = sinf(now * 2.0f + (float)i) * 1.2f;
            draw_line(canvas, 25.0f, 40.0f, 3.0f, 36.0f + dy + wob,
                      1, eye_color);
            draw_line(canvas, 128.0f - 25.0f, 40.0f, 128.0f - 3.0f,
                      36.0f + dy + wob, 1, eye_color);
        }
        const float nx = 64.0f;
        const float ny = 47.0f;
        const float nose[3][2] = {
            {nx - 3.0f, ny - 1.0f}, {nx + 3.0f, ny - 1.0f}, {nx, ny + 2.0f},
        };
        fill_polygon(canvas, nose, 3, eye_color);
        draw_arc(canvas, nx - 6.0f, ny + 1.0f, nx, ny + 6.0f, 0, 180, 1,
                 eye_color);
        draw_arc(canvas, nx, ny + 1.0f, nx + 6.0f, ny + 6.0f, 0, 180, 1,
                 eye_color);
    }
}

static void render_face_layers(face_canvas_t *canvas,
                               const desk_mode_view_t *desk,
                               const copet_behavior_view_t *behavior)
{
    const color_t background = rgb332(0, 0, 0);
    const color_t eye_color = rgb332(150, 255, 70);
    const float now = desk != NULL
        ? desk->animation_time_ms / 1000.0f
        : behavior != NULL ? behavior->elapsed_ms / 1000.0f : 0.0f;
    face_pose_t pose = build_base_pose(desk, now);
    apply_behavior_mood_layer(&pose, behavior);
    apply_behavior_gesture_layer(&pose, behavior);
    apply_behavior_transform_layer(&pose, behavior);

    const bool base_neutral = behavior == NULL ||
        behavior->id == COPET_BEHAVIOR_NEUTRAL;
    const float left_x = 41.0f + pose.gaze_x + pose.motion_x;
    const float right_x = 87.0f + pose.gaze_x + pose.motion_x;
    const float center_y = 32.0f + pose.gaze_y + pose.motion_y;
    if (base_neutral && desk != NULL &&
        desk->expression == DESK_EXPRESSION_SLEEPY) {
        /* Upstream sleepy is a bare mood: it draws its own nap timeline. */
        draw_sleepy(canvas, now, eye_color);
    } else if (behavior != NULL && behavior->id == COPET_BEHAVIOR_LOVELY) {
        /* Smitten: two hearts beat where the eyes would be. */
        const float beat = heart_beat(behavior->elapsed_ms / 1000.0f);
        const float size = 28.0f * (1.0f + 0.16f * beat);
        draw_heart(canvas, left_x, center_y + pose.left_y_offset,
                   size, eye_color);
        draw_heart(canvas, right_x, center_y + pose.right_y_offset,
                   size, eye_color);
    } else {
        /* Upstream engine: tilt skews each eye's y (+right/-left); bias makes
         * the near eye swell (right *(1+bias), left *(1-bias)). */
        const float es_left = 1.0f - pose.bias;
        const float es_right = 1.0f + pose.bias;
        draw_eye(canvas, left_x, center_y + pose.left_y_offset - pose.tilt,
                 pose.eye_width * es_left, pose.left_height * es_left, false,
                 pose.expression, pose.behavior_id, eye_color, background);
        draw_eye(canvas, right_x, center_y + pose.right_y_offset + pose.tilt,
                 pose.eye_width * es_right, pose.right_height * es_right, true,
                 pose.expression, pose.behavior_id, eye_color, background);
    }

    draw_behavior_overlay_layer(canvas, behavior, eye_color);

    const bool behavior_is_neutral = base_neutral;
    if (behavior_is_neutral && desk != NULL &&
        desk->vibe == DESK_VIBE_SMOKING) {
        draw_smoking(canvas, now, eye_color, rgb332(65, 170, 60));
    } else if (behavior_is_neutral && desk != NULL &&
               desk->vibe == DESK_VIBE_OVERHEATED) {
        draw_heat(canvas, now, rgb332(210, 250, 65));
    }
}

void pip_face_port_render(uint8_t *framebuffer, int screen_width,
                          int screen_height, int x, int y,
                          int width, int height,
                          const desk_mode_view_t *view,
                          const copet_behavior_view_t *behavior)
{
    if (framebuffer == NULL || view == NULL || screen_width <= 0 ||
        screen_height <= 0 || width <= 0 || height <= 0) {
        return;
    }
    face_canvas_t canvas = {
        framebuffer, screen_width, screen_height, x, y, width, height,
    };
    render_face_layers(&canvas, view, behavior);
}
