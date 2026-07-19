#include "modes/focus_mode.h"

void focus_mode_init(focus_mode_t *focus)
{
    focus->state = FOCUS_TIMER_READY;
    focus->break_phase = false;
    focus->remaining_seconds = FOCUS_WORK_SECONDS;
    focus->sessions = 0;
    focus->last_tick_us = 0;
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
            focus->remaining_seconds = FOCUS_BREAK_SECONDS;
        } else {
            focus->break_phase = false;
            focus->remaining_seconds = FOCUS_WORK_SECONDS;
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
