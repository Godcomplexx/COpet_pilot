#ifndef COPET_ASSISTANT_MODE_H
#define COPET_ASSISTANT_MODE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Assistant Mode sub-state machine (M7). Pure logic: it models the on-screen
 * flow of a text query -- pick a preset, send it, wait, then show the answer or
 * an error -- but performs no I/O itself. The network lives in
 * services/assistant_service; app_main feeds answers/errors back in as events.
 * See docs/27_assistant_m7_plan.md.
 */

typedef enum {
    ASSISTANT_IDLE,      /* choosing a preset query */
    ASSISTANT_WAITING,   /* request in flight (has a client-side timeout) */
    ASSISTANT_RESULT,    /* showing the answer card */
    ASSISTANT_ERROR,     /* showing a recoverable error */
    /* Deferred voice path (see the plan); not driven yet. */
    ASSISTANT_RECORDING,
    ASSISTANT_UPLOADING,
} assistant_state_t;

enum {
    ASSISTANT_TEXT_MAX = 192,   /* answer/error text shown on the card */
    ASSISTANT_MOOD_MAX = 16,    /* answer mood tag, e.g. "helpful" */
    ASSISTANT_TIMEOUT_MS = 30000, /* generous: a local LLM can take a while */
};

typedef struct {
    const char *label; /* menu label, e.g. "WEATHER" */
    const char *type;  /* contract "type" field, e.g. "weather" */
    const char *text;  /* query text sent to the API */
} assistant_preset_t;

typedef struct {
    assistant_state_t state;
    uint8_t selected;                     /* preset index (IDLE selection) */
    uint32_t request_started_ms;
    char result_text[ASSISTANT_TEXT_MAX]; /* answer or error, for the card */
    char result_mood[ASSISTANT_MOOD_MAX]; /* answer mood (empty on error) */
    bool has_result;
} assistant_mode_t;

void assistant_mode_init(assistant_mode_t *assistant);

/* Preset table. */
uint8_t assistant_mode_preset_count(void);
const assistant_preset_t *assistant_mode_preset_at(uint8_t index);
const assistant_preset_t *assistant_mode_selected_preset(
    const assistant_mode_t *assistant);

/* IDLE only: move the selection by steps (wraps both ways). */
void assistant_mode_scroll(assistant_mode_t *assistant, int32_t steps);

/*
 * IDLE -> WAITING. Returns the preset the caller should submit to the service,
 * or NULL when not in a submittable state (so input is ignored while waiting).
 */
const assistant_preset_t *assistant_mode_submit(assistant_mode_t *assistant,
                                                uint32_t now_ms);

/* WAITING -> RESULT: copy the answer text + mood for the card. */
void assistant_mode_on_answer(assistant_mode_t *assistant, const char *text,
                              const char *mood, uint32_t now_ms);

/* WAITING -> ERROR: copy the error text (mood cleared). */
void assistant_mode_on_error(assistant_mode_t *assistant, const char *text,
                             uint32_t now_ms);

/*
 * Show a result directly, from any state -- for a local skill (e.g. the
 * triple-tap clock) that answers on-device without going through the service.
 */
void assistant_mode_show_result(assistant_mode_t *assistant, const char *text,
                                const char *mood);

/* Advance timers: a WAITING request past the timeout becomes ERROR. Returns
 * true when the state changed. */
bool assistant_mode_tick(assistant_mode_t *assistant, uint32_t now_ms);

/* Cancel/back: WAITING/RESULT/ERROR -> IDLE. */
void assistant_mode_back(assistant_mode_t *assistant);

const char *assistant_mode_state_label(assistant_state_t state);

#endif
