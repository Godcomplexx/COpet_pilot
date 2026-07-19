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

static void cover_lid(face_canvas_t *canvas, float x, float y,
                      float width, float height, bool is_right,
                      float inner, float outer, color_t background)
{
    for (int column = 0; column <= (int)width; ++column) {
        const float t = width > 0.0f ? column / width : 0.0f;
        const float left_depth = is_right ? inner : outer;
        const float right_depth = is_right ? outer : inner;
        const float depth = left_depth + (right_depth - left_depth) * t;
        fill_rect(canvas, x + column, y - 1.0f, 1.2f,
                  height * depth + 1.0f, background);
    }
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
        cover_lid(canvas, x, y, width, height, is_right,
                  0.38f, 0.52f, background);
    }

    if (behavior_id == COPET_BEHAVIOR_FOCUSED) {
        cover_lid(canvas, x, y, width, height, is_right,
                  0.08f, 0.28f, background);
    } else if (behavior_id == COPET_BEHAVIOR_ANGRY) {
        cover_lid(canvas, x, y, width, height, is_right,
                  0.48f, 0.05f, background);
    }
}

static void draw_die(face_canvas_t *canvas, float center_x, float center_y,
                     float size, int value,
                     color_t eye_color, color_t background)
{
    const float x = center_x - size * 0.5f;
    const float y = center_y - size * 0.5f;
    fill_round_rect(canvas, x, y, size, size, 2.0f, eye_color);
    const float left = x + size * 0.27f;
    const float middle = x + size * 0.50f;
    const float right = x + size * 0.73f;
    const float top = y + size * 0.27f;
    const float center = y + size * 0.50f;
    const float bottom = y + size * 0.73f;
    const float radius = 2.2f;

#define PIP(px, py) fill_ellipse(canvas, (px) - radius, (py) - radius, \
                                 (px) + radius, (py) + radius, background)
    if (value == 1 || value == 3 || value == 5) { PIP(middle, center); }
    if (value >= 2) { PIP(left, top); PIP(right, bottom); }
    if (value >= 4) { PIP(right, top); PIP(left, bottom); }
    if (value == 6) { PIP(left, center); PIP(right, center); }
#undef PIP
}

static void draw_z(face_canvas_t *canvas, float x, float y,
                   float size, color_t color)
{
    draw_line(canvas, x, y, x + size, y, 1, color);
    draw_line(canvas, x + size, y, x, y + size, 1, color);
    draw_line(canvas, x, y + size, x + size, y + size, 1, color);
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
        pose->eye_width = 30.0f;
        pose->eye_height = 44.0f;
        pose->gaze_x = 0.0f;
        pose->gaze_y = 0.0f;
        break;
    case COPET_BEHAVIOR_ATTENTIVE:
        pose->eye_width = 38.0f;
        pose->eye_height = 42.0f *
            (1.0f + 0.04f * sinf(elapsed_s * 3.14159265f * 2.0f));
        pose->gaze_x = 0.0f;
        pose->gaze_y = 0.0f;
        break;
    case COPET_BEHAVIOR_FOCUSED:
        pose->eye_width = 40.0f;
        pose->eye_height = 24.0f;
        pose->gaze_x = sinf(elapsed_s * 1.4f) * 0.7f;
        pose->gaze_y = 3.0f;
        break;
    case COPET_BEHAVIOR_ANGRY:
        pose->eye_width = 40.0f;
        pose->eye_height = 28.0f;
        pose->gaze_x = 0.0f;
        pose->gaze_y = 0.0f;
        break;
    case COPET_BEHAVIOR_DISORIENTED: {
        const float phase = behavior_progress(behavior) *
                            3.14159265f * 2.0f;
        pose->eye_width = 32.0f;
        pose->eye_height = 36.0f;
        pose->gaze_x = cosf(phase) * 6.0f;
        pose->gaze_y = sinf(phase) * 6.0f;
        pose->left_y_offset = -3.0f;
        pose->right_y_offset = 3.0f;
        break;
    }
    case COPET_BEHAVIOR_CONNECTING: {
        const float phase = (behavior->elapsed_ms % 800U) / 800.0f;
        pose->eye_width = 34.0f;
        pose->eye_height = 34.0f;
        pose->gaze_x = -9.0f + phase * 18.0f;
        pose->gaze_y = 0.0f;
        break;
    }
    case COPET_BEHAVIOR_ZEN:
        pose->eye_width = 34.0f;
        pose->eye_height = 5.0f;
        pose->gaze_x = 0.0f;
        pose->gaze_y = 0.0f;
        pose->motion_y += sinf(elapsed_s * 3.14159265f * 2.0f / 2.4f) *
                          2.0f;
        break;
    case COPET_BEHAVIOR_DICE_ROLL:
        pose->eye_width = 30.0f;
        pose->eye_height = 30.0f;
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
        const uint32_t phase = behavior->elapsed_ms;
        float openness = 1.0f;
        if (phase < 80U) {
            openness = 1.0f - 0.92f * smoothstep(phase / 80.0f);
        } else if (phase < 160U) {
            openness = 0.08f + 0.92f *
                smoothstep((phase - 80U) / 80.0f);
        } else if (phase < 240U) {
            openness = 1.0f;
        } else if (phase < 320U) {
            openness = 1.0f - 0.92f *
                smoothstep((phase - 240U) / 80.0f);
        } else {
            openness = 0.08f + 0.92f *
                smoothstep((phase - 320U) / 160.0f);
        }
        pose->left_height *= openness;
        pose->right_height *= openness;
    } else if (behavior->id == COPET_BEHAVIOR_LOOK_LEFT ||
               behavior->id == COPET_BEHAVIOR_LOOK_RIGHT) {
        const uint32_t elapsed = behavior->elapsed_ms;
        float amount;
        if (elapsed < 150U) {
            amount = smoothstep(elapsed / 150.0f);
        } else if (elapsed < 900U) {
            amount = 1.0f;
        } else {
            amount = 1.0f - smoothstep((elapsed - 900U) / 300.0f);
        }
        pose->gaze_x = amount *
            (behavior->id == COPET_BEHAVIOR_LOOK_LEFT ? -9.0f : 9.0f);
    }
}

/* Layer 4: reactions move the complete eye group after shape selection. */
static void apply_behavior_transform_layer(
    face_pose_t *pose, const copet_behavior_view_t *behavior)
{
    if (behavior == NULL) { return; }
    if (behavior->id == COPET_BEHAVIOR_ACKNOWLEDGE) {
        const float progress = behavior_progress(behavior);
        const float nod = sinf(progress * 3.14159265f);
        pose->motion_y += nod * 4.0f;
        const float openness = 1.0f - nod * 0.25f;
        pose->left_height *= openness;
        pose->right_height *= openness;
    } else if (behavior->id == COPET_BEHAVIOR_ANGRY &&
               behavior->elapsed_ms < 350U) {
        const float envelope = 1.0f - behavior->elapsed_ms / 350.0f;
        pose->motion_x += sinf(behavior->elapsed_ms * 0.11f) *
                          3.0f * envelope;
    }
}

static int dice_value(const copet_behavior_view_t *behavior, int offset)
{
    uint32_t elapsed = behavior->elapsed_ms;
    if (behavior->duration_ms > 1200U &&
        elapsed > behavior->duration_ms - 1200U) {
        elapsed = behavior->duration_ms - 1200U;
    }
    const uint32_t step = elapsed / 120U;
    return (int)((step * 5U + (uint32_t)offset * 2U) % 6U) + 1;
}

/* Layer 5: action overlays are drawn after the eyes, before screen HUD. */
static void draw_behavior_overlay_layer(
    face_canvas_t *canvas, const copet_behavior_view_t *behavior,
    color_t eye_color)
{
    if (behavior == NULL) { return; }
    if (behavior->id == COPET_BEHAVIOR_ALERT) {
        const float x = behavior_progress(behavior) * 128.0f;
        draw_line(canvas, x, 3.0f, x, 61.0f, 1, eye_color);
    } else if (behavior->id == COPET_BEHAVIOR_CONNECTING) {
        const float phase = (behavior->elapsed_ms % 800U) / 800.0f;
        const float x = 12.0f + phase * 104.0f;
        draw_line(canvas, x, 5.0f, x, 59.0f, 1, eye_color);
    } else if (behavior->id == COPET_BEHAVIOR_ZEN) {
        const int count = 1 + (int)((behavior->elapsed_ms / 400U) % 3U);
        for (int index = 0; index < count; ++index) {
            const float x = 58.0f + index * 6.0f;
            fill_ellipse(canvas, x - 1.5f, 13.0f - 1.5f,
                         x + 1.5f, 13.0f + 1.5f, eye_color);
        }
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

    const float left_x = 41.0f + pose.gaze_x + pose.motion_x;
    const float right_x = 87.0f + pose.gaze_x + pose.motion_x;
    const float center_y = 32.0f + pose.gaze_y + pose.motion_y;
    if (behavior != NULL && behavior->id == COPET_BEHAVIOR_DICE_ROLL) {
        draw_die(canvas, left_x, center_y + pose.left_y_offset,
                 pose.eye_width, dice_value(behavior, 0),
                 eye_color, background);
        draw_die(canvas, right_x, center_y + pose.right_y_offset,
                 pose.eye_width, dice_value(behavior, 1),
                 eye_color, background);
    } else {
        draw_eye(canvas, left_x, center_y + pose.left_y_offset,
                 pose.eye_width, pose.left_height, false,
                 pose.expression, pose.behavior_id, eye_color, background);
        draw_eye(canvas, right_x, center_y + pose.right_y_offset,
                 pose.eye_width, pose.right_height, true,
                 pose.expression, pose.behavior_id, eye_color, background);
    }

    draw_behavior_overlay_layer(canvas, behavior, eye_color);

    const bool behavior_is_neutral = behavior == NULL ||
        behavior->id == COPET_BEHAVIOR_NEUTRAL;
    if (behavior_is_neutral && desk != NULL &&
        desk->expression == DESK_EXPRESSION_SLEEPY &&
        fmodf(now, 8.55f) > 5.9f && fmodf(now, 8.55f) < 7.3f) {
        draw_z(canvas, 105.0f, 25.0f, 5.0f, eye_color);
        draw_z(canvas, 112.0f, 16.0f, 7.0f, eye_color);
    }
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

void pip_face_port_render_compact(
    uint8_t *framebuffer, int screen_width, int screen_height,
    int x, int y, int width, int height,
    const copet_behavior_view_t *behavior)
{
    if (framebuffer == NULL || behavior == NULL || screen_width <= 0 ||
        screen_height <= 0 || width <= 0 || height <= 0) {
        return;
    }
    face_canvas_t canvas = {
        framebuffer, screen_width, screen_height, x, y, width, height,
    };
    render_face_layers(&canvas, NULL, behavior);
}
