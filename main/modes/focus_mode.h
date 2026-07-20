#ifndef COPET_FOCUS_MODE_H
#define COPET_FOCUS_MODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Durations of the default preset (25/5). Kept as the reset baseline. */
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
    uint8_t preset;
    uint32_t remaining_seconds;
    uint32_t sessions;
    int64_t last_tick_us;
} focus_mode_t;

/* Reset to a fresh work session of the default preset, ready but not running. */
void focus_mode_init(focus_mode_t *focus);

/* Number of selectable work/break presets. */
size_t focus_mode_preset_count(void);

/* Work / break duration (seconds) of the currently selected preset. */
uint32_t focus_mode_work_seconds(const focus_mode_t *focus);
uint32_t focus_mode_break_seconds(const focus_mode_t *focus);

/* Short label of the current preset, e.g. "25/5". */
const char *focus_mode_preset_label(const focus_mode_t *focus);

/*
 * Cycle the preset by the given encoder steps (wraps both directions). Only
 * allowed while the timer is READY; ignored once it is running or paused so a
 * session's length cannot change mid-run. Resets the remaining time to the new
 * preset's current-phase duration.
 */
void focus_mode_select_preset(focus_mode_t *focus, int32_t steps);

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
