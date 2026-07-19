#ifndef COPET_DESK_MODE_H
#define COPET_DESK_MODE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DESK_COMFORT_SENSOR_ERROR,
    DESK_COMFORT_COLD,
    DESK_COMFORT_DRY,
    DESK_COMFORT_COMFY,
    DESK_COMFORT_HUMID,
    DESK_COMFORT_HOT,
} desk_comfort_t;

typedef enum {
    DESK_EXPRESSION_NEUTRAL,
    DESK_EXPRESSION_HAPPY,
    DESK_EXPRESSION_BORED,
    DESK_EXPRESSION_SLEEPY,
    DESK_EXPRESSION_SCARED,
    DESK_EXPRESSION_CHILL,
    DESK_EXPRESSION_TIRED,
    DESK_EXPRESSION_SURPRISED,
} desk_expression_t;

typedef enum {
    DESK_VIBE_NONE,
    DESK_VIBE_SHIVER,
    DESK_VIBE_SMOKING,
    DESK_VIBE_OVERHEATED,
} desk_vibe_t;

typedef enum {
    DESK_MOTION_NONE,
    DESK_MOTION_MOVED,
    DESK_MOTION_TILTED,
    DESK_MOTION_SHAKEN,
    DESK_MOTION_FALLING,
} desk_motion_event_t;

typedef enum {
    DESK_TOUCH_NONE,
    DESK_TOUCH_HAPPY,
    DESK_TOUCH_EXCITED,
    DESK_TOUCH_WINK,
} desk_touch_reaction_t;

typedef struct {
    bool sensor_ok;
    bool reacting;
    bool motion_available;
    float temperature_c;
    float humidity_percent;
    desk_comfort_t comfort;
    desk_expression_t expression;
    desk_vibe_t vibe;
    desk_motion_event_t motion_event;
    desk_touch_reaction_t touch_reaction;
    int8_t gaze_x;
    int8_t gaze_y;
    int8_t bob_y;
    uint8_t eye_open_percent;
    uint32_t uptime_seconds;
    uint32_t inactivity_seconds;
    uint32_t animation_time_ms;
    uint32_t effect_elapsed_ms;
    uint32_t reaction_elapsed_ms;
} desk_mode_view_t;

typedef struct {
    desk_mode_view_t view;
    uint32_t started_ms;
    uint32_t last_activity_ms;
    uint32_t next_blink_ms;
    uint32_t blink_started_ms;
    uint32_t next_gaze_ms;
    uint32_t reaction_started_ms;
    uint32_t reaction_until_ms;
    uint32_t motion_reaction_until_ms;
    uint32_t vibe_started_ms;
    uint8_t gaze_step;
    uint8_t touch_reaction_step;
    bool blinking;
} desk_mode_t;

void desk_mode_init(desk_mode_t *desk, uint32_t now_ms);
void desk_mode_set_environment(desk_mode_t *desk, bool sensor_ok,
                               float temperature_c,
                               float humidity_percent);
void desk_mode_on_touch(desk_mode_t *desk, uint32_t now_ms);
void desk_mode_on_activity(desk_mode_t *desk, uint32_t now_ms);
desk_motion_event_t desk_mode_set_motion_sample(
    desk_mode_t *desk, bool available,
    float accel_x_g, float accel_y_g, float accel_z_g,
    float gyro_x_dps, float gyro_y_dps, float gyro_z_dps,
    uint32_t now_ms);
void desk_mode_update(desk_mode_t *desk, uint32_t now_ms);
const desk_mode_view_t *desk_mode_get_view(const desk_mode_t *desk);
desk_comfort_t desk_mode_classify_comfort(bool sensor_ok,
                                          float temperature_c,
                                          float humidity_percent);
const char *desk_mode_comfort_label(desk_comfort_t comfort);
const char *desk_mode_expression_label(desk_expression_t expression);
const char *desk_mode_vibe_label(desk_vibe_t vibe);
const char *desk_mode_motion_label(desk_motion_event_t event);
const char *desk_mode_touch_reaction_label(desk_touch_reaction_t reaction);

#endif
