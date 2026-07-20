#include "modes/focus_mode.h"

typedef struct {
    uint16_t work_minutes;
    uint16_t break_minutes;
    const char *label;
} focus_preset_t;

/* Preset 0 stays 25/5 so it matches FOCUS_WORK_SECONDS/FOCUS_BREAK_SECONDS. */
static const focus_preset_t FOCUS_PRESETS[] = {
    {25, 5, "25/5"},
    {50, 10, "50/10"},
    {60, 20, "60/20"},
    {90, 20, "90/20"},
};

#define FOCUS_PRESET_COUNT \
    (sizeof(FOCUS_PRESETS) / sizeof(FOCUS_PRESETS[0]))

size_t focus_mode_preset_count(void)
{
    return FOCUS_PRESET_COUNT;
}

uint32_t focus_mode_work_seconds(const focus_mode_t *focus)
{
    return (uint32_t)FOCUS_PRESETS[focus->preset].work_minutes * 60U;
}

uint32_t focus_mode_break_seconds(const focus_mode_t *focus)
{
    return (uint32_t)FOCUS_PRESETS[focus->preset].break_minutes * 60U;
}

const char *focus_mode_preset_label(const focus_mode_t *focus)
{
    return FOCUS_PRESETS[focus->preset].label;
}

void focus_mode_init(focus_mode_t *focus)
{
    focus->state = FOCUS_TIMER_READY;
    focus->break_phase = false;
    focus->preset = 0;
    focus->remaining_seconds = focus_mode_work_seconds(focus);
    focus->sessions = 0;
    focus->last_tick_us = 0;
}

void focus_mode_select_preset(focus_mode_t *focus, int32_t steps)
{
    if (focus->state != FOCUS_TIMER_READY) {
        return;
    }
    const int32_t count = (int32_t)FOCUS_PRESET_COUNT;
    int32_t index = ((int32_t)focus->preset + steps) % count;
    if (index < 0) { index += count; }
    focus->preset = (uint8_t)index;
    focus->remaining_seconds = focus->break_phase
        ? focus_mode_break_seconds(focus)
        : focus_mode_work_seconds(focus);
}

focus_toggle_result_t focus_mode_toggle(focus_mode_t *focus, int64_t now_us)
{
    if (focus->state == FOCUS_TIMER_RUNNING) {
        focus->state = FOCUS_TIMER_PAUSED;
        return FOCUS_TOGGLE_PAUSED;
    }

    const focus_toggle_result_t result =
        focus->state == FOCUS_TIMER_READY
            ? FOCUS_TOGGLE_STARTED
            : FOCUS_TOGGLE_RESUMED;
    focus->state = FOCUS_TIMER_RUNNING;
    focus->last_tick_us = now_us;
    return result;
}

bool focus_mode_tick(focus_mode_t *focus, int64_t now_us)
{
    if (focus->state != FOCUS_TIMER_RUNNING) {
        return false;
    }

    const uint32_t elapsed_seconds =
        (uint32_t)((now_us - focus->last_tick_us) / 1000000);
    if (elapsed_seconds == 0) {
        return false;
    }

    focus->last_tick_us += (int64_t)elapsed_seconds * 1000000;
    if (elapsed_seconds >= focus->remaining_seconds) {
        if (!focus->break_phase) {
            ++focus->sessions;
            focus->break_phase = true;
            focus->remaining_seconds = focus_mode_break_seconds(focus);
        } else {
            focus->break_phase = false;
            focus->remaining_seconds = focus_mode_work_seconds(focus);
        }
        focus->state = FOCUS_TIMER_READY;
    } else {
        focus->remaining_seconds -= elapsed_seconds;
    }
    return true;
}

void focus_mode_pause(focus_mode_t *focus)
{
    if (focus->state == FOCUS_TIMER_RUNNING) {
        focus->state = FOCUS_TIMER_PAUSED;
    }
}

const char *focus_mode_status_label(const focus_mode_t *focus)
{
    if (focus->state == FOCUS_TIMER_RUNNING) {
        return focus->break_phase ? "BREAK RUN" : "WORK RUN";
    }
    if (focus->state == FOCUS_TIMER_PAUSED) {
        return focus->break_phase ? "BREAK PAUSE" : "WORK PAUSE";
    }
    return focus->break_phase ? "BREAK READY" : "WORK READY";
}

const char *focus_mode_action_hint(const focus_mode_t *focus)
{
    switch (focus->state) {
    case FOCUS_TIMER_RUNNING:
        return "TOUCH PAUSE";
    case FOCUS_TIMER_PAUSED:
        return "TOUCH RESUME";
    case FOCUS_TIMER_READY:
    default:
        return "TOUCH START";
    }
}
