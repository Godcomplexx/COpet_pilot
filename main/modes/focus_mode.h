#ifndef COPET_FOCUS_MODE_H
#define COPET_FOCUS_MODE_H

#include <stdbool.h>
#include <stdint.h>

enum {
    FOCUS_WORK_SECONDS = 25 * 60,
    FOCUS_BREAK_SECONDS = 5 * 60,
};

typedef enum {
    FOCUS_TIMER_READY,
    FOCUS_TIMER_RUNNING,
    FOCUS_TIMER_PAUSED,
} focus_timer_state_t;

typedef enum {
    FOCUS_TOGGLE_STARTED,
    FOCUS_TOGGLE_PAUSED,
    FOCUS_TOGGLE_RESUMED,
} focus_toggle_result_t;

typedef struct {
    focus_timer_state_t state;
    bool break_phase;
    uint32_t remaining_seconds;
    uint32_t sessions;
    int64_t last_tick_us;
} focus_mode_t;

/* Reset to a fresh 25-minute work session, ready but not running. */
void focus_mode_init(focus_mode_t *focus);

/*
 * Short touch: start a ready timer, pause a running timer, or resume a paused
 * timer. The result lets the integration layer attach one sound to the exact
 * transition without duplicating timer-state logic.
 */
focus_toggle_result_t focus_mode_toggle(focus_mode_t *focus, int64_t now_us);

/*
 * Advance a running timer to now_us. Completed phases flip work<->break,
 * increment the session counter after work, and return the timer to READY.
 * Returns true when the view changed and a redraw is needed.
 */
bool focus_mode_tick(focus_mode_t *focus, int64_t now_us);

/* Leaving Focus (home): pause a running timer so the remaining time is kept. */
void focus_mode_pause(focus_mode_t *focus);

const char *focus_mode_status_label(const focus_mode_t *focus);
const char *focus_mode_action_hint(const focus_mode_t *focus);

#endif
