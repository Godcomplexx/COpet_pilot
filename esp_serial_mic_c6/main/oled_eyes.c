/*
 * oled_eyes.c — анимированные "глаза" на OLED SSD1306 (128x64, I2C) для ESP32-C6.
 * Аналог идеи playfultechnology/esp32-eyes, но на чистом ESP-IDF.
 *
 * Подключение (XIAO ESP32-C6): SDA = D4 / GPIO22, SCL = D5 / GPIO23, адрес 0x3C.
 * Глаза меняются по состоянию: покой (моргание) / запись (внимательные) / речь.
 */

#include "oled_eyes.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#define OLED_SDA   22
#define OLED_SCL   23
#define OLED_ADDR_PRIMARY  0x3C
#define OLED_ADDR_FALLBACK 0x3D
#define OLED_W     128
#define OLED_H     64
#define MPU6050_ADDR_PRIMARY  0x68
#define MPU6050_ADDR_FALLBACK 0x69
#define ADXL345_ADDR_PRIMARY  0x53
#define ADXL345_ADDR_FALLBACK 0x1D
#define ACCEL_TILT_THRESHOLD  1500
#define ACCEL_SHAKE_THRESHOLD 4500
#define ACCEL_FLAT_DELTA_THRESHOLD 4500

#define TAG "oled_eyes"

typedef enum {
    ACCEL_NONE = 0,
    ACCEL_MPU6050,
    ACCEL_ADXL345,
} accel_kind_t;

typedef struct {
    int w;
    int h;
    int top_r;
    int bottom_r;
    int slope_top;
    int slope_bottom;
    int offset_x;
    int offset_y;
} eye_pose_t;

static i2c_master_dev_handle_t s_dev;
static i2c_master_dev_handle_t s_accel_dev;
static uint8_t s_fb[OLED_W * OLED_H / 8];
static volatile face_state_t s_state = FACE_IDLE;
static uint8_t s_oled_addr = 0;
static uint8_t s_accel_addr = 0;
static accel_kind_t s_accel_kind = ACCEL_NONE;

// ── низкоуровневый SSD1306 ─────────────────────────────────
static esp_err_t cmd(uint8_t c)
{
    uint8_t b[2] = {0x00, c};
    return i2c_master_transmit(s_dev, b, 2, 100);
}

static bool is_manual_state(face_state_t state)
{
    return state == FACE_LISTENING ||
        state == FACE_PROCESSING ||
        state == FACE_SPEAKING;
}

static esp_err_t accel_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t b[2] = {reg, value};
    return i2c_master_transmit(s_accel_dev, b, sizeof(b), 100);
}

static bool accel_read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_accel_dev, &reg, 1, value, 1, 100) == ESP_OK;
}

static bool mpu_read_accel(int16_t *ax, int16_t *ay, int16_t *az)
{
    uint8_t reg = 0x3B;
    uint8_t b[6] = {0};
    if (i2c_master_transmit_receive(s_accel_dev, &reg, 1, b, sizeof(b), 100) != ESP_OK) {
        return false;
    }
    *ax = (int16_t)((b[0] << 8) | b[1]);
    *ay = (int16_t)((b[2] << 8) | b[3]);
    *az = (int16_t)((b[4] << 8) | b[5]);
    return true;
}

static bool adxl345_read_accel(int16_t *ax, int16_t *ay, int16_t *az)
{
    uint8_t reg = 0x32;
    uint8_t b[6] = {0};
    if (i2c_master_transmit_receive(s_accel_dev, &reg, 1, b, sizeof(b), 100) != ESP_OK) {
        return false;
    }

    int16_t raw_x = (int16_t)((b[1] << 8) | b[0]);
    int16_t raw_y = (int16_t)((b[3] << 8) | b[2]);
    int16_t raw_z = (int16_t)((b[5] << 8) | b[4]);
    *ax = raw_x * 64;
    *ay = raw_y * 64;
    *az = raw_z * 64;
    return true;
}

static bool accel_read_accel(int16_t *ax, int16_t *ay, int16_t *az)
{
    if (s_accel_kind == ACCEL_MPU6050) {
        return mpu_read_accel(ax, ay, az);
    }
    if (s_accel_kind == ACCEL_ADXL345) {
        return adxl345_read_accel(ax, ay, az);
    }
    return false;
}

// SH1106 (часто 1.3") не понимает горизонтальную адресацию SSD1306 и имеет
// сдвиг RAM на 2 столбца. Постраничная адресация ниже работает на ОБОИХ.
// Если дисплей реально SSD1306 0.96" и картинка уехала на 2px — поставь 0.
#define OLED_COL_OFFSET 0

static bool ssd1306_init(void)
{
    static const uint8_t seq[] = {
        0xAE, 0x20, 0x02, 0xB0, 0xC8, 0x00, 0x10, 0x40,  // 0x20,0x02 = постраничный режим
        0x81, 0x7F, 0xA1, 0xA6, 0xA8, 0x3F, 0xA4,
        0xD3, 0x00, 0xD5, 0x80, 0xD9, 0xF1, 0xDA, 0x12,
        0xDB, 0x40, 0x8D, 0x14, 0xAF,
    };
    for (size_t i = 0; i < sizeof(seq); i++) {
        if (cmd(seq[i]) != ESP_OK) {
            return false;
        }
    }
    return true;
}

static void flush(void)
{
    uint8_t line[1 + OLED_W];
    line[0] = 0x40;                // признак "данные"
    for (int page = 0; page < 8; page++) {
        cmd(0xB0 | page);                              // выбрать страницу
        cmd(0x00 | (OLED_COL_OFFSET & 0x0F));          // младший ниббл столбца
        cmd(0x10 | (OLED_COL_OFFSET >> 4));            // старший ниббл столбца
        memcpy(&line[1], &s_fb[page * OLED_W], OLED_W);
        i2c_master_transmit(s_dev, line, sizeof(line), 100);
    }
}

// ── примитивы рисования в буфер ────────────────────────────
static void fill_screen(bool on)
{
    memset(s_fb, on ? 0xFF : 0x00, sizeof(s_fb));
    flush();
}

static inline void px(int x, int y)
{
    if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) return;
    s_fb[x + (y / 8) * OLED_W] |= (uint8_t)(1u << (y & 7));
}

static void fill_rect(int x, int y, int w, int h)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            px(x + i, y + j);
}

static void draw_line(int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        px(x0, y0);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void fill_circle(int cx, int cy, int r)
{
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r)
                px(cx + dx, cy + dy);
}

// Залитый прямоугольник со скруглёнными углами (тело "глаза").
static void fill_round_rect(int x, int y, int w, int h, int r)
{
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    fill_rect(x + r, y, w - 2 * r, h);
    fill_rect(x, y + r, w, h - 2 * r);
    fill_circle(x + r, y + r, r);
    fill_circle(x + w - r - 1, y + r, r);
    fill_circle(x + r, y + h - r - 1, r);
    fill_circle(x + w - r - 1, y + h - r - 1, r);
}

// Один глаз с текущей высотой (моргание = маленькая высота).
static void draw_eye(int cx, int cy, int w, int h, int r)
{
    fill_round_rect(cx - w / 2, cy - h / 2, w, h, r);
}

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static int approach_int(int value, int target, int step)
{
    if (value < target) return value + clamp_int(target - value, 1, step);
    if (value > target) return value - clamp_int(value - target, 1, step);
    return value;
}

static eye_pose_t eye_pose_for_state(face_state_t state)
{
    switch (state) {
        case FACE_LISTENING:
            return (eye_pose_t){42, 42, 12, 12, 0, 0, 0, -1};
        case FACE_PROCESSING:
            return (eye_pose_t){42, 16, 3, 8, -4, -2, 0, -4};
        case FACE_SPEAKING:
            return (eye_pose_t){40, 24, 8, 14, 0, 2, 0, 0};
        case FACE_TILT_LEFT:
            return (eye_pose_t){40, 34, 8, 10, -3, 2, -2, 0};
        case FACE_TILT_RIGHT:
            return (eye_pose_t){40, 34, 8, 10, 3, -2, 2, 0};
        case FACE_SHAKE:
            return (eye_pose_t){44, 44, 16, 16, 0, 0, 0, -1};
        case FACE_FLAT:
            return (eye_pose_t){40, 12, 3, 8, -4, -4, 0, 2};
        case FACE_LOOK_UP:
            return (eye_pose_t){42, 42, 14, 12, 0, 0, 0, -3};
        case FACE_LOOK_DOWN:
            return (eye_pose_t){42, 18, 4, 10, -3, 3, 0, 3};
        case FACE_HAPPY:
            return (eye_pose_t){40, 22, 8, 16, 0, 2, 0, 4};
        case FACE_IDLE:
        default:
            return (eye_pose_t){40, 34, 8, 8, 0, 0, 0, 0};
    }
}

static void approach_pose(eye_pose_t *value, const eye_pose_t *target)
{
    value->w = approach_int(value->w, target->w, 2);
    value->h = approach_int(value->h, target->h, 3);
    value->top_r = approach_int(value->top_r, target->top_r, 2);
    value->bottom_r = approach_int(value->bottom_r, target->bottom_r, 2);
    value->slope_top = approach_int(value->slope_top, target->slope_top, 1);
    value->slope_bottom = approach_int(value->slope_bottom, target->slope_bottom, 1);
    value->offset_x = approach_int(value->offset_x, target->offset_x, 1);
    value->offset_y = approach_int(value->offset_y, target->offset_y, 1);
}

static void draw_param_eye(int cx, int cy, const eye_pose_t *pose, bool mirror, int look_x, int bob)
{
    int w = clamp_int(pose->w, 8, 54);
    int h = clamp_int(pose->h, 4, 50);
    int half_w = w / 2;
    int center_x = cx + (mirror ? -pose->offset_x : pose->offset_x) + look_x;
    int center_y = cy + pose->offset_y + bob;
    int left = center_x - half_w;
    int right = center_x + half_w;
    int top_base = center_y - h / 2;
    int bottom_base = center_y + h / 2;
    int slope_top = mirror ? -pose->slope_top : pose->slope_top;
    int slope_bottom = mirror ? -pose->slope_bottom : pose->slope_bottom;
    int top_r = clamp_int(pose->top_r, 0, half_w);
    int bottom_r = clamp_int(pose->bottom_r, 0, half_w);

    for (int y = top_base - 8; y <= bottom_base + 8; y++) {
        for (int x = left; x <= right; x++) {
            int rel2 = (x - center_x) * 2;
            int top = top_base + (slope_top * rel2) / w;
            int bottom = bottom_base + (slope_bottom * rel2) / w;
            if (y < top || y > bottom) {
                continue;
            }

            int edge_top = y - top;
            int edge_bottom = bottom - y;
            int inset = 0;
            if (edge_top < top_r) {
                int top_inset = top_r - edge_top;
                if (top_inset > inset) inset = top_inset;
            }
            if (edge_bottom < bottom_r) {
                int bottom_inset = bottom_r - edge_bottom;
                if (bottom_inset > inset) inset = bottom_inset;
            }
            if (x >= left + inset && x <= right - inset) {
                px(x, y);
            }
        }
    }
}

static int look_for_state(face_state_t state)
{
    if (state == FACE_TILT_LEFT) return -8;
    if (state == FACE_TILT_RIGHT) return 8;
    return 0;
}

static int look_y_for_state(face_state_t state)
{
    if (state == FACE_LOOK_UP) return -8;
    if (state == FACE_LOOK_DOWN) return 7;
    return 0;
}

// ── задача анимации ────────────────────────────────────────
static void eyes_task(void *arg)
{
    (void)arg;
    {
        const int lx = 42;
        const int rx = 86;
        int cy = 32;
        int look_x = 0, look_y = 0;       // текущий взгляд
        int look_tx = 0, look_ty = 0;     // цель взгляда
        int idle_move = 18;               // кадров до смены взгляда в покое
        int blink_timer = 30;
        int blink_phase = 0;
        int double_blink = 0;
        int mood = 0;                     // 0 нет, 1 радость, 2 любопытство
        int mood_frames = 0;
        int mood_timer = 70;
        int t = 0;
        eye_pose_t cur = eye_pose_for_state(FACE_IDLE);
        static uint8_t prev_fb[sizeof(s_fb)];

        while (1) {
            face_state_t st = s_state;
            bool idle = (st == FACE_IDLE);
            eye_pose_t target = eye_pose_for_state(st);
            int target_cy = (st == FACE_PROCESSING) ? 30 : 32;

            if (idle) {
                // живой взгляд: периодически смотрим по сторонам/вверх
                if (--idle_move <= 0) {
                    int r = rand() % 100;
                    if (r < 50)      { look_tx = 0;   look_ty = 0; }
                    else if (r < 68) { look_tx = -15; look_ty = 0; }
                    else if (r < 86) { look_tx = 15;  look_ty = 0; }
                    else             { look_tx = (rand() % 2) ? 8 : -8; look_ty = -6; }
                    idle_move = 12 + rand() % 28;
                }
                // изредка микро-эмоция
                if (mood == 0 && --mood_timer <= 0) {
                    mood = 1 + rand() % 3;
                    mood_frames = 16;
                    mood_timer = 60 + rand() % 90;
                }
            } else {
                look_tx = look_for_state(st);
                look_ty = look_y_for_state(st);
                mood = 0;
                mood_frames = 0;
            }
            if (mood_frames > 0 && --mood_frames == 0) mood = 0;

            // микро-эмоция меняет целевую позу
            if (idle && mood == 1) {            // радость: нижние веки дугой
                target.h = 22; target.bottom_r = 16; target.offset_y = 3;
            } else if (idle && mood == 2) {     // любопытство: выше + взгляд вверх
                target.h = 42; target.top_r = 14; look_ty = -6;
            } else if (idle && mood == 3) {
                target.h = 18; target.top_r = 4; target.bottom_r = 10;
                target.slope_top = -3; target.slope_bottom = 3;
            }

            approach_pose(&cur, &target);
            cy = approach_int(cy, target_cy, 1);
            look_x = approach_int(look_x, look_tx, 3);
            look_y = approach_int(look_y, look_ty, 2);

            // моргание (иногда двойное)
            if (blink_phase == 0 && st != FACE_PROCESSING && st != FACE_FLAT && --blink_timer <= 0) {
                blink_phase = 6;
                double_blink = (rand() % 100 < 25) ? 1 : 0;
                blink_timer = 18 + rand() % 40;
            }

            eye_pose_t draw_pose = cur;
            if (blink_phase > 0) {
                int k = blink_phase > 3 ? (6 - blink_phase) : blink_phase;
                draw_pose.h = cur.h - k * (cur.h / 3);
                if (draw_pose.h < 4) draw_pose.h = 4;
                blink_phase--;
                if (blink_phase == 0 && double_blink > 0) { blink_phase = 6; double_blink--; }
            }

            int bob = (st == FACE_SPEAKING) ? ((t / 2) % 2 ? 2 : -2) : 0;

            memset(s_fb, 0, sizeof(s_fb));
            draw_param_eye(lx, cy + look_y, &draw_pose, false, look_x, bob);
            draw_param_eye(rx, cy + look_y, &draw_pose, true, look_x, bob);

            if (st == FACE_PROCESSING) {
                int active_dot = (t / 2) % 3;
                for (int i = 0; i < 3; i++) {
                    int size = i == active_dot ? 5 : 3;
                    fill_rect(53 + i * 10, 53, size, 3);
                }
            } else if (st == FACE_FLAT) {
                draw_line(26, 23, 58, 19);
                draw_line(70, 19, 102, 23);
            } else if (st == FACE_SHAKE) {
                fill_circle(lx, 52, 2);
                fill_circle(rx, 52, 2);
            } else if (st == FACE_HAPPY) {
                draw_line(55, 52, 60, 55);
                draw_line(60, 55, 68, 55);
                draw_line(68, 55, 73, 52);
            }

            if (memcmp(s_fb, prev_fb, sizeof(s_fb)) != 0) {
                flush();
                memcpy(prev_fb, s_fb, sizeof(s_fb));
            }

            t++;
            vTaskDelay(pdMS_TO_TICKS(110));
        }
    }

    const int eye_w = 34;
    const int gap = 24;
    const int lx = OLED_W / 2 - gap / 2 - eye_w / 2;   // центр левого глаза
    const int rx = OLED_W / 2 + gap / 2 + eye_w / 2;   // центр правого глаза

    int cur_h = 34;          // текущая высота (сглаживание)
    int blink_timer = 30;    // кадров до следующего моргания
    int blink_phase = 0;     // 0 = нет; >0 идёт анимация моргания
    int t = 0;

    while (1) {
        face_state_t st = s_state;

        int target_w = eye_w;
        int target_h, cy;
        int eye_shift = 0;
        bool draw_processing_dots = false;
        bool draw_sleepy = false;
        bool draw_surprised = false;
        bool draw_tilt_brows = false;
        switch (st) {
            case FACE_LISTENING: target_h = 44; cy = 30; break;  // внимательные, крупнее
            case FACE_PROCESSING:
                target_h = 18;
                cy = 26;
                draw_processing_dots = true;
                break;  // думает: прищур + бегущие точки
            case FACE_SPEAKING:  target_h = 30; cy = 30; break;
            case FACE_TILT_LEFT:
                target_h = 30;
                cy = 30;
                eye_shift = -8;
                draw_tilt_brows = true;
                break;
            case FACE_TILT_RIGHT:
                target_h = 30;
                cy = 30;
                eye_shift = 8;
                draw_tilt_brows = true;
                break;
            case FACE_SHAKE:
                target_w = 38;
                target_h = 46;
                cy = 30;
                draw_surprised = true;
                break;
            case FACE_FLAT:
                target_h = 12;
                cy = 32;
                draw_sleepy = true;
                break;
            default:             target_h = 34; cy = 32; break;  // покой
        }

        // плавное приближение высоты к целевой
        if (cur_h < target_h) cur_h += 3;
        else if (cur_h > target_h) cur_h -= 3;

        // моргание
        if (blink_phase == 0 && --blink_timer <= 0) {
            blink_phase = 6;                       // длительность моргания (кадров)
            blink_timer = 24 + (rand() % 40);      // ~2-5 c до следующего
        }
        int draw_h = cur_h;
        if (blink_phase > 0) {
            int k = blink_phase > 3 ? (6 - blink_phase) : blink_phase; // 0..3..0
            draw_h = cur_h - k * (cur_h / 3);
            if (draw_h < 4) draw_h = 4;
            blink_phase--;
        }

        // лёгкое "дыхание"/покачивание по вертикали
        int bob = (st == FACE_SPEAKING) ? ((t / 2) % 2 ? 2 : -2) : 0;

        static uint8_t prev_fb[sizeof(s_fb)];
        memset(s_fb, 0, sizeof(s_fb));
        draw_eye(lx + eye_shift, cy + bob, target_w, draw_h, 8);
        draw_eye(rx + eye_shift, cy + bob, target_w, draw_h, 8);
        if (draw_processing_dots) {
            int active_dot = (t / 2) % 3;
            for (int i = 0; i < 3; i++) {
                fill_circle(54 + i * 10, 54, i == active_dot ? 3 : 2);
            }
        }
        if (draw_sleepy) {
            draw_line(35, 22, 57, 18);
            draw_line(71, 18, 93, 22);
        }
        if (draw_surprised) {
            fill_circle(lx, 52, 2);
            fill_circle(rx, 52, 2);
        }
        if (draw_tilt_brows) {
            int lean = st == FACE_TILT_LEFT ? -5 : 5;
            draw_line(31, 17 + lean, 57, 17 - lean);
            draw_line(71, 17 - lean, 97, 17 + lean);
        }

        // Перерисовываем только если кадр изменился — в покое статично, без мерцания.
        if (memcmp(s_fb, prev_fb, sizeof(s_fb)) != 0) {
            flush();
            memcpy(prev_fb, s_fb, sizeof(s_fb));
        }

        t++;
        vTaskDelay(pdMS_TO_TICKS(140));   // темп анимации (больше = медленнее/спокойнее)
    }
}

void oled_eyes_set_state(face_state_t state)
{
    s_state = state;
}

static void accel_task(void *arg)
{
    (void)arg;
    int32_t base_ax = 0;
    int32_t base_ay = 0;
    int32_t base_az = 0;
    int samples = 0;

    while (samples < 12) {
        int16_t ax, ay, az;
        if (accel_read_accel(&ax, &ay, &az)) {
            base_ax += ax;
            base_ay += ay;
            base_az += az;
            samples++;
        }
        vTaskDelay(pdMS_TO_TICKS(80));
    }

    base_ax /= samples;
    base_ay /= samples;
    base_az /= samples;
    ESP_LOGW(TAG, "Accel neutral ax=%ld ay=%ld az=%ld",
             (long)base_ax, (long)base_ay, (long)base_az);

    int16_t prev_ax = (int16_t)base_ax;
    int16_t prev_ay = (int16_t)base_ay;
    int16_t prev_az = (int16_t)base_az;
    int shake_frames = 0;
    int accel_hold_frames = 0;
    face_state_t accel_hold_state = FACE_IDLE;
    int log_counter = 0;

    while (1) {
        int16_t ax, ay, az;
        if (!accel_read_accel(&ax, &ay, &az)) {
            ESP_LOGW(TAG, "Accelerometer read failed");
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }

        int dx = ax - base_ax;
        int dy = ay - base_ay;
        int dz = az - base_az;
        int motion = abs(ax - prev_ax) + abs(ay - prev_ay) + abs(az - prev_az);
        prev_ax = ax;
        prev_ay = ay;
        prev_az = az;

        if (++log_counter >= 6) {
            ESP_LOGW(TAG, "Accel raw ax=%d ay=%d az=%d delta=%d/%d/%d motion=%d state=%d",
                     ax, ay, az, dx, dy, dz, motion, (int)s_state);
            log_counter = 0;
        }

        face_state_t triggered = FACE_IDLE;
        int adx = abs(dx);
        int ady = abs(dy);
        int adz = abs(dz);

        if (motion > ACCEL_SHAKE_THRESHOLD * 3) {
            triggered = FACE_SHAKE;
            shake_frames = 16;
        } else if (adx > ACCEL_TILT_THRESHOLD || ady > ACCEL_TILT_THRESHOLD) {
            if (adx >= ady) {
                triggered = dx > 0 ? FACE_TILT_RIGHT : FACE_TILT_LEFT;
            } else {
                triggered = dy > 0 ? FACE_LOOK_UP : FACE_LOOK_DOWN;
            }
        } else if (adz > ACCEL_FLAT_DELTA_THRESHOLD && adx < 4500 && ady < 4500) {
            triggered = FACE_FLAT;
        } else if (motion > ACCEL_SHAKE_THRESHOLD) {
            triggered = FACE_HAPPY;
        }

        if (triggered != FACE_IDLE) {
            accel_hold_state = triggered;
            accel_hold_frames = triggered == FACE_SHAKE ? 18 : 10;
        }

        face_state_t current = s_state;
        if (!is_manual_state(current)) {
            face_state_t next = FACE_IDLE;
            if (shake_frames > 0) {
                next = FACE_SHAKE;
                shake_frames--;
            } else if (accel_hold_frames > 0) {
                next = accel_hold_state;
                accel_hold_frames--;
            }
            if (next != current) {
                ESP_LOGW(TAG, "Accel face state -> %d", (int)next);
                s_state = next;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(160));
    }
}

static void i2c_scan(i2c_master_bus_handle_t bus)
{
    bool any = false;
    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        if (i2c_master_probe(bus, addr, 50) == ESP_OK) {
            ESP_LOGW(TAG, "I2C device found at 0x%02X", addr);
            any = true;
        }
    }
    if (!any) {
        ESP_LOGW(TAG, "I2C scan found no devices on SDA=%d SCL=%d", OLED_SDA, OLED_SCL);
    }
}

static void try_start_accelerometer(i2c_master_bus_handle_t bus)
{
    uint8_t found_addr = 0;
    accel_kind_t found_kind = ACCEL_NONE;
    if (i2c_master_probe(bus, MPU6050_ADDR_PRIMARY, 100) == ESP_OK) {
        found_addr = MPU6050_ADDR_PRIMARY;
        found_kind = ACCEL_MPU6050;
    } else if (i2c_master_probe(bus, MPU6050_ADDR_FALLBACK, 100) == ESP_OK) {
        found_addr = MPU6050_ADDR_FALLBACK;
        found_kind = ACCEL_MPU6050;
    } else if (i2c_master_probe(bus, ADXL345_ADDR_PRIMARY, 100) == ESP_OK) {
        found_addr = ADXL345_ADDR_PRIMARY;
        found_kind = ACCEL_ADXL345;
    } else if (i2c_master_probe(bus, ADXL345_ADDR_FALLBACK, 100) == ESP_OK) {
        found_addr = ADXL345_ADDR_FALLBACK;
        found_kind = ACCEL_ADXL345;
    }

    if (found_addr == 0) {
        ESP_LOGW(TAG, "Accelerometer not found on MPU6050 0x68/0x69 or ADXL345 0x53/0x1D; emotions disabled");
        return;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = found_addr,
        .scl_speed_hz = 400000,
    };
    if (i2c_master_bus_add_device(bus, &dev_cfg, &s_accel_dev) != ESP_OK) {
        ESP_LOGW(TAG, "Accelerometer add device failed");
        return;
    }

    s_accel_addr = found_addr;
    s_accel_kind = found_kind;
    if (s_accel_kind == ACCEL_MPU6050) {
        accel_write_reg(0x6B, 0x00);  // wake up
        accel_write_reg(0x1C, 0x00);  // accel +/-2g
        uint8_t whoami = 0;
        if (accel_read_reg(0x75, &whoami)) {
            ESP_LOGW(TAG, "MPU6050 WHO_AM_I=0x%02X", whoami);
        } else {
            ESP_LOGW(TAG, "MPU6050 WHO_AM_I read failed");
        }
        ESP_LOGW(TAG, "MPU6050 emotions enabled at 0x%02X", s_accel_addr);
    } else {
        accel_write_reg(0x2D, 0x00);  // standby before config
        accel_write_reg(0x31, 0x08);  // full resolution, +/-2g
        accel_write_reg(0x2C, 0x0A);  // 100 Hz
        accel_write_reg(0x2D, 0x08);  // measurement mode
        uint8_t devid = 0;
        if (accel_read_reg(0x00, &devid)) {
            ESP_LOGW(TAG, "ADXL345 DEVID=0x%02X", devid);
            if (devid != 0xE5) {
                ESP_LOGW(TAG, "ADXL345 unexpected DEVID; still trying to read it as ADXL345");
            }
        } else {
            ESP_LOGW(TAG, "ADXL345 DEVID read failed");
        }
        ESP_LOGW(TAG, "ADXL345 emotions enabled at 0x%02X", s_accel_addr);
    }
    xTaskCreate(accel_task, "accel_emotions", 3072, NULL, 2, NULL);
}

void oled_eyes_start(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = OLED_SDA,
        .scl_io_num = OLED_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) {
        ESP_LOGW(TAG, "I2C bus init failed — без дисплея");
        return;
    }

    i2c_scan(bus);
    try_start_accelerometer(bus);

    uint8_t found_addr = 0;
    if (i2c_master_probe(bus, OLED_ADDR_PRIMARY, 100) == ESP_OK) {
        found_addr = OLED_ADDR_PRIMARY;
    } else if (i2c_master_probe(bus, OLED_ADDR_FALLBACK, 100) == ESP_OK) {
        found_addr = OLED_ADDR_FALLBACK;
    }

    if (found_addr == 0) {
        ESP_LOGW(TAG, "OLED not found on 0x3C/0x3D; check SDA=22 SCL=23 VCC GND");
        return;
    }
    s_oled_addr = found_addr;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = found_addr,
        .scl_speed_hz = 400000,   // быстрее заливка кадра -> нет мерцания
    };
    if (i2c_master_bus_add_device(bus, &dev_cfg, &s_dev) != ESP_OK) {
        ESP_LOGW(TAG, "OLED add device failed");
        return;
    }

    if (!ssd1306_init()) {
        ESP_LOGW(TAG, "SSD1306 не отвечает (проверь SDA=22/SCL=23/адрес 0x3C)");
        return;
    }

    fill_screen(false);   // просто очистить экран (без стартовой белой вспышки)
    xTaskCreate(eyes_task, "oled_eyes", 4096, NULL, 2, NULL);
    ESP_LOGI(TAG, "OLED глаза запущены");
}
