#include "modes/desk_mode.h"

#include <stddef.h>

enum {
    BLINK_DURATION_MS = 280,
    TOUCH_REACTION_MS = 900,
    MOTION_REACTION_MS = 1600,
    BOB_PERIOD_MS = 2400,
    BORED_AFTER_MS = 30000,
    SMOKING_AFTER_MS = 90000,
    SLEEPY_AFTER_MS = 10 * 60 * 1000,
    CLOCK_SHOW_MS = 6000, /* how long the triple-tap clock stays on the face */
    /* Idle life, ported from the upstream engine (engine.py): random glances on
     * a varied cadence rather than a fixed pattern, so a resting face never
     * loops. _IDLE_GAP=(1.5,5.0)s glances, _BLINK_GAP=(2.0,6.0)s blinks. */
    GAZE_GAP_MIN_MS = 1500,
    GAZE_GAP_MAX_MS = 5000,
    BLINK_GAP_MIN_MS = 2000,
    BLINK_GAP_MAX_MS = 6000,
    GAZE_RECENTER_PCT = 30,   /* chance a glance returns to centre */
    GAZE_DART_BLINK_PCT = 40, /* chance the eyes blink as they dart */
    GAZE_X_SPAN = 16,         /* glance target x in [-16, 16] */
    GAZE_Y_SPAN = 7,          /* glance target y in [-7, 7] */
    /* Time-constant for gliding the gaze to its target (upstream _TAU_GAZE):
     * the eyes ease across instead of teleporting. */
    GAZE_TAU_MS = 90,
};

static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

/* Deterministic xorshift32 so the idle life is lively yet host-testable. */
static uint32_t desk_rand(desk_mode_t *desk)
{
    uint32_t value = desk->random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    desk->random_state = value;
    return value;
}

/* Uniform in [lo, hi]. */
static uint32_t desk_rand_range(desk_mode_t *desk, uint32_t lo, uint32_t hi)
{
    return lo + desk_rand(desk) % (hi - lo + 1U);
}

static float abs_float(float value)
{
    return value < 0.0f ? -value : value;
}

desk_comfort_t desk_mode_classify_comfort(bool sensor_ok,
                                          float temperature_c,
                                          float humidity_percent)
{
    if (!sensor_ok) { return DESK_COMFORT_SENSOR_ERROR; }
    if (temperature_c < 18.0f) { return DESK_COMFORT_COLD; }
    if (temperature_c > 32.0f) { return DESK_COMFORT_HOT; }
    if (humidity_percent < 30.0f) { return DESK_COMFORT_DRY; }
    if (humidity_percent > 70.0f) { return DESK_COMFORT_HUMID; }
    return DESK_COMFORT_COMFY;
}

const char *desk_mode_comfort_label(desk_comfort_t comfort)
{
    switch (comfort) {
    case DESK_COMFORT_COLD: return "COLD";
    case DESK_COMFORT_DRY: return "DRY";
    case DESK_COMFORT_COMFY: return "COMFY";
    case DESK_COMFORT_HUMID: return "HUMID";
    case DESK_COMFORT_HOT: return "HOT";
    case DESK_COMFORT_SENSOR_ERROR:
    default: return "SENSOR";
    }
}

const char *desk_mode_expression_label(desk_expression_t expression)
{
    switch (expression) {
    case DESK_EXPRESSION_HAPPY: return "HAPPY";
    case DESK_EXPRESSION_BORED: return "BORED";
    case DESK_EXPRESSION_SLEEPY: return "SLEEPY";
    case DESK_EXPRESSION_SCARED: return "SCARED";
    case DESK_EXPRESSION_CHILL: return "CHILL";
    case DESK_EXPRESSION_TIRED: return "TIRED";
    case DESK_EXPRESSION_SURPRISED: return "WOW";
    case DESK_EXPRESSION_NEUTRAL:
    default: return "NEUTRAL";
    }
}

const char *desk_mode_vibe_label(desk_vibe_t vibe)
{
    switch (vibe) {
    case DESK_VIBE_SHIVER: return "SHIVER";
    case DESK_VIBE_SMOKING: return "SMOKING";
    case DESK_VIBE_OVERHEATED: return "OVERHEATED";
    case DESK_VIBE_NONE:
    default: return "NONE";
    }
}

const char *desk_mode_motion_label(desk_motion_event_t event)
{
    switch (event) {
    case DESK_MOTION_MOVED: return "MOVED";
    case DESK_MOTION_TILTED: return "TILTED";
    case DESK_MOTION_SHAKEN: return "SHAKEN";
    case DESK_MOTION_FALLING: return "FALLING";
    case DESK_MOTION_NONE:
    default: return "NONE";
    }
}

const char *desk_mode_touch_reaction_label(desk_touch_reaction_t reaction)
{
    switch (reaction) {
    case DESK_TOUCH_HAPPY: return "HAPPY";
    case DESK_TOUCH_EXCITED: return "EXCITED";
    case DESK_TOUCH_WINK: return "WINK";
    case DESK_TOUCH_NONE:
    default: return "NONE";
    }
}

void desk_mode_init(desk_mode_t *desk, uint32_t now_ms)
{
    if (desk == NULL) { return; }
    *desk = (desk_mode_t){0};
    desk->started_ms = now_ms;
    desk->last_activity_ms = now_ms;
    desk->next_blink_ms = now_ms + 1800U;
    desk->next_gaze_ms = now_ms + GAZE_GAP_MIN_MS;
    desk->last_update_ms = now_ms;
    /* Seed the idle-life RNG; xorshift needs a non-zero state. */
    desk->random_state = (now_ms * 2654435761U) | 1U;
    desk->view.eye_open_percent = 100;
    desk->view.comfort = DESK_COMFORT_SENSOR_ERROR;
    desk->view.expression = DESK_EXPRESSION_NEUTRAL;
}

void desk_mode_set_environment(desk_mode_t *desk, bool sensor_ok,
                               float temperature_c,
                               float humidity_percent)
{
    if (desk == NULL) { return; }
    desk->view.sensor_ok = sensor_ok;
    desk->view.temperature_c = temperature_c;
    desk->view.humidity_percent = humidity_percent;
    desk->view.comfort = desk_mode_classify_comfort(
        sensor_ok, temperature_c, humidity_percent);
}

void desk_mode_on_activity(desk_mode_t *desk, uint32_t now_ms)
{
    if (desk == NULL) { return; }
    desk->last_activity_ms = now_ms;
}

void desk_mode_on_touch(desk_mode_t *desk, uint32_t now_ms)
{
    if (desk == NULL) { return; }
    desk_mode_on_activity(desk, now_ms);
    desk->view.touch_reaction =
        (desk_touch_reaction_t)(DESK_TOUCH_HAPPY +
                                desk->touch_reaction_step % 3U);
    ++desk->touch_reaction_step;
    desk->reaction_started_ms = now_ms;
    desk->reaction_until_ms = now_ms + TOUCH_REACTION_MS;
    desk->view.reacting = true;
    desk->view.gaze_x = 0;
    desk->view.gaze_y = -2;
}

desk_motion_event_t desk_mode_set_motion_sample(
    desk_mode_t *desk, bool available,
    float accel_x_g, float accel_y_g, float accel_z_g,
    float gyro_x_dps, float gyro_y_dps, float gyro_z_dps,
    uint32_t now_ms)
{
    if (desk == NULL) { return DESK_MOTION_NONE; }
    desk->view.motion_available = available;
    if (!available) {
        desk->view.motion_event = DESK_MOTION_NONE;
        return DESK_MOTION_NONE;
    }

    const float magnitude_squared = accel_x_g * accel_x_g +
                                    accel_y_g * accel_y_g +
                                    accel_z_g * accel_z_g;
    const float gyro_sum = abs_float(gyro_x_dps) +
                           abs_float(gyro_y_dps) +
                           abs_float(gyro_z_dps);
    desk_motion_event_t event = DESK_MOTION_NONE;
    if (magnitude_squared < 0.20f) {
        event = DESK_MOTION_FALLING;              /* near free-fall -> scared */
    } else if (magnitude_squared > 4.0f) {
        /* A sharp acceleration spike (~>2g) is a hit/knock -> angry. Rotation
         * (high gyro) alone is carrying/handling, not a hit. */
        event = DESK_MOTION_SHAKEN;
    } else if (abs_float(accel_z_g) < 0.55f) {
        event = DESK_MOTION_TILTED;               /* tilted off flat -> dizzy */
    } else if (abs_float(magnitude_squared - 1.0f) > 0.18f ||
               gyro_sum > 35.0f) {
        event = DESK_MOTION_MOVED;                /* carried/rotated -> dizzy */
    }

    if (event != DESK_MOTION_NONE && event != desk->view.motion_event) {
        desk->motion_reaction_until_ms = now_ms + MOTION_REACTION_MS;
        desk_mode_on_activity(desk, now_ms);
    }
    desk->view.motion_event = event;
    return event;
}

static void select_expression(desk_mode_t *desk, uint32_t now_ms)
{
    desk_expression_t expression = DESK_EXPRESSION_NEUTRAL;
    desk_vibe_t vibe = DESK_VIBE_NONE;
    const bool motion_reacting =
        desk->motion_reaction_until_ms != 0U &&
        !time_reached(now_ms, desk->motion_reaction_until_ms);

    if (motion_reacting) {
        if (desk->view.motion_event == DESK_MOTION_MOVED) {
            expression = DESK_EXPRESSION_SURPRISED;
        } else {
            expression = DESK_EXPRESSION_SCARED;
            vibe = DESK_VIBE_SHIVER;
        }
    } else if (desk->view.reacting) {
        expression = desk->view.touch_reaction == DESK_TOUCH_WINK
            ? DESK_EXPRESSION_NEUTRAL
            : DESK_EXPRESSION_HAPPY;
    } else if (desk->view.comfort == DESK_COMFORT_HOT) {
        expression = DESK_EXPRESSION_SCARED;
        vibe = DESK_VIBE_OVERHEATED;
    } else if (desk->view.comfort == DESK_COMFORT_COLD) {
        expression = DESK_EXPRESSION_TIRED;
        vibe = DESK_VIBE_SHIVER;
    } else if (desk->view.inactivity_seconds * 1000U >= SLEEPY_AFTER_MS) {
        expression = DESK_EXPRESSION_SLEEPY;
    } else if (desk->view.inactivity_seconds * 1000U >= SMOKING_AFTER_MS) {
        expression = DESK_EXPRESSION_CHILL;
        vibe = DESK_VIBE_SMOKING;
    } else if (desk->view.inactivity_seconds * 1000U >= BORED_AFTER_MS) {
        expression = DESK_EXPRESSION_BORED;
    } else if (desk->view.comfort == DESK_COMFORT_DRY ||
               desk->view.comfort == DESK_COMFORT_HUMID) {
        expression = DESK_EXPRESSION_TIRED;
    }

    desk->view.expression = expression;
    if (vibe != desk->view.vibe) {
        desk->view.vibe = vibe;
        desk->vibe_started_ms = now_ms;
    }
    desk->view.effect_elapsed_ms = now_ms - desk->vibe_started_ms;
}

void desk_mode_show_clock(desk_mode_t *desk, int hour, int minute,
                          uint32_t now_ms)
{
    if (desk == NULL) { return; }
    desk->view.clock_active = true;
    desk->view.clock_hour = (int8_t)hour;
    desk->view.clock_minute = (int8_t)minute;
    desk->clock_until_ms = now_ms + CLOCK_SHOW_MS;
    desk_mode_on_activity(desk, now_ms);
}

void desk_mode_update(desk_mode_t *desk, uint32_t now_ms)
{
    if (desk == NULL) { return; }

    if (desk->view.clock_active && time_reached(now_ms, desk->clock_until_ms)) {
        desk->view.clock_active = false;
    }

    desk->view.uptime_seconds = (now_ms - desk->started_ms) / 1000U;
    desk->view.inactivity_seconds = (now_ms - desk->last_activity_ms) / 1000U;
    desk->view.animation_time_ms = now_ms - desk->started_ms;
    desk->view.reacting = desk->reaction_until_ms != 0U &&
                          !time_reached(now_ms, desk->reaction_until_ms);
    desk->view.reaction_elapsed_ms = now_ms - desk->reaction_started_ms;

    /* Idle glance: pick a new target -- mostly a random dart, sometimes a
     * return to centre; the eyes tend to blink as they dart (upstream engine
     * idle-glance behavior). The visible gaze then glides to the target below. */
    if (!desk->view.reacting && time_reached(now_ms, desk->next_gaze_ms)) {
        if (desk_rand(desk) % 100U < GAZE_RECENTER_PCT) {
            desk->gaze_target_x = 0;
            desk->gaze_target_y = 0;
        } else {
            desk->gaze_target_x =
                (int8_t)((int)(desk_rand(desk) % (2U * GAZE_X_SPAN + 1U)) -
                         GAZE_X_SPAN);
            desk->gaze_target_y =
                (int8_t)((int)(desk_rand(desk) % (2U * GAZE_Y_SPAN + 1U)) -
                         GAZE_Y_SPAN);
            if (!desk->blinking &&
                desk_rand(desk) % 100U < GAZE_DART_BLINK_PCT) {
                desk->blinking = true;
                desk->blink_started_ms = now_ms;
            }
        }
        desk->next_gaze_ms =
            now_ms + desk_rand_range(desk, GAZE_GAP_MIN_MS, GAZE_GAP_MAX_MS);
    }

    /* Glide the visible gaze toward the target (exponential ease over dt). */
    {
        const uint32_t dt_ms = now_ms - desk->last_update_ms;
        desk->last_update_ms = now_ms;
        const float alpha =
            (float)dt_ms / ((float)dt_ms + (float)GAZE_TAU_MS);
        desk->gaze_cur_x +=
            ((float)desk->gaze_target_x - desk->gaze_cur_x) * alpha;
        desk->gaze_cur_y +=
            ((float)desk->gaze_target_y - desk->gaze_cur_y) * alpha;
        desk->view.gaze_x = (int8_t)(desk->gaze_cur_x +
            (desk->gaze_cur_x >= 0.0f ? 0.5f : -0.5f));
        desk->view.gaze_y = (int8_t)(desk->gaze_cur_y +
            (desk->gaze_cur_y >= 0.0f ? 0.5f : -0.5f));
    }

    if (!desk->blinking && time_reached(now_ms, desk->next_blink_ms)) {
        desk->blinking = true;
        desk->blink_started_ms = now_ms;
    }
    if (desk->blinking) {
        const uint32_t elapsed_ms = now_ms - desk->blink_started_ms;
        if (elapsed_ms >= BLINK_DURATION_MS) {
            desk->blinking = false;
            desk->view.eye_open_percent = 100;
            desk->next_blink_ms = now_ms +
                desk_rand_range(desk, BLINK_GAP_MIN_MS, BLINK_GAP_MAX_MS);
        } else {
            const uint32_t half_ms = BLINK_DURATION_MS / 2U;
            const uint32_t distance = elapsed_ms < half_ms
                ? half_ms - elapsed_ms : elapsed_ms - half_ms;
            desk->view.eye_open_percent =
                (uint8_t)(8U + (distance * 92U) / half_ms);
        }
    }

    const uint32_t bob_phase = now_ms % BOB_PERIOD_MS;
    desk->view.bob_y = bob_phase < BOB_PERIOD_MS / 4U ? 0 :
        bob_phase < BOB_PERIOD_MS / 2U ? 1 :
        bob_phase < (BOB_PERIOD_MS * 3U) / 4U ? 0 : -1;
    select_expression(desk, now_ms);
}

const desk_mode_view_t *desk_mode_get_view(const desk_mode_t *desk)
{
    return desk == NULL ? NULL : &desk->view;
}
