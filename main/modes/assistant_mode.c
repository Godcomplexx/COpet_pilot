#include "modes/assistant_mode.h"

#include <string.h>

/* Preset queries. `type` matches the Cloud API contract; the first prototype
 * may route them all through /v1/query. Text is UTF-8. */
static const assistant_preset_t PRESETS[] = {
    {"WEATHER", "weather", "\xD0\x9A\xD0\xB0\xD0\xBA\xD0\xB0\xD1\x8F "
                           "\xD0\xBF\xD0\xBE\xD0\xB3\xD0\xBE\xD0\xB4\xD0\xB0 "
                           "\xD1\x81\xD0\xB5\xD0\xB3\xD0\xBE\xD0\xB4\xD0\xBD"
                           "\xD1\x8F?"}, /* "Какая погода сегодня?" */
    {"TIME", "time", "\xD0\x9A\xD0\xBE\xD1\x82\xD0\xBE\xD1\x80\xD1\x8B\xD0\xB9 "
                     "\xD1\x87\xD0\xB0\xD1\x81?"}, /* "Который час?" */
    {"HELLO", "query", "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82!"},
    /* "Привет!" */
};

enum { PRESET_COUNT = (uint8_t)(sizeof(PRESETS) / sizeof(PRESETS[0])) };

static void copy_bounded(char *dst, size_t cap, const char *src)
{
    if (cap == 0U) { return; }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    for (; i + 1U < cap && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

void assistant_mode_init(assistant_mode_t *assistant)
{
    if (assistant == NULL) { return; }
    *assistant = (assistant_mode_t){0};
    assistant->state = ASSISTANT_IDLE;
}

uint8_t assistant_mode_preset_count(void)
{
    return PRESET_COUNT;
}

const assistant_preset_t *assistant_mode_preset_at(uint8_t index)
{
    return index < PRESET_COUNT ? &PRESETS[index] : NULL;
}

const assistant_preset_t *assistant_mode_selected_preset(
    const assistant_mode_t *assistant)
{
    if (assistant == NULL) { return NULL; }
    return assistant_mode_preset_at(assistant->selected);
}

void assistant_mode_scroll(assistant_mode_t *assistant, int32_t steps)
{
    if (assistant == NULL || assistant->state != ASSISTANT_IDLE) { return; }
    int32_t index = (int32_t)assistant->selected + steps;
    index %= (int32_t)PRESET_COUNT;
    if (index < 0) { index += (int32_t)PRESET_COUNT; }
    assistant->selected = (uint8_t)index;
}

const assistant_preset_t *assistant_mode_submit(assistant_mode_t *assistant,
                                                uint32_t now_ms)
{
    if (assistant == NULL || assistant->state != ASSISTANT_IDLE) {
        return NULL; /* single in-flight request: ignore input while busy */
    }
    assistant->state = ASSISTANT_WAITING;
    assistant->request_started_ms = now_ms;
    assistant->has_result = false;
    assistant->result_text[0] = '\0';
    assistant->result_mood[0] = '\0';
    return assistant_mode_selected_preset(assistant);
}

void assistant_mode_on_answer(assistant_mode_t *assistant, const char *text,
                              const char *mood, uint32_t now_ms)
{
    (void)now_ms;
    if (assistant == NULL || assistant->state != ASSISTANT_WAITING) { return; }
    copy_bounded(assistant->result_text, sizeof(assistant->result_text), text);
    copy_bounded(assistant->result_mood, sizeof(assistant->result_mood), mood);
    assistant->has_result = true;
    assistant->state = ASSISTANT_RESULT;
}

void assistant_mode_on_error(assistant_mode_t *assistant, const char *text,
                             uint32_t now_ms)
{
    (void)now_ms;
    if (assistant == NULL || assistant->state != ASSISTANT_WAITING) { return; }
    copy_bounded(assistant->result_text, sizeof(assistant->result_text), text);
    assistant->result_mood[0] = '\0';
    assistant->has_result = false;
    assistant->state = ASSISTANT_ERROR;
}

bool assistant_mode_tick(assistant_mode_t *assistant, uint32_t now_ms)
{
    if (assistant == NULL || assistant->state != ASSISTANT_WAITING) {
        return false;
    }
    if ((uint32_t)(now_ms - assistant->request_started_ms) <
        (uint32_t)ASSISTANT_TIMEOUT_MS) {
        return false;
    }
    assistant->result_mood[0] = '\0';
    assistant->has_result = false;
    assistant->state = ASSISTANT_ERROR;
    return true;
}

void assistant_mode_back(assistant_mode_t *assistant)
{
    if (assistant == NULL) { return; }
    if (assistant->state == ASSISTANT_WAITING ||
        assistant->state == ASSISTANT_RESULT ||
        assistant->state == ASSISTANT_ERROR) {
        assistant->state = ASSISTANT_IDLE;
        assistant->has_result = false;
        assistant->result_text[0] = '\0';
        assistant->result_mood[0] = '\0';
    }
}

const char *assistant_mode_state_label(assistant_state_t state)
{
    switch (state) {
    case ASSISTANT_WAITING: return "WAITING";
    case ASSISTANT_RESULT: return "RESULT";
    case ASSISTANT_ERROR: return "ERROR";
    case ASSISTANT_RECORDING: return "RECORDING";
    case ASSISTANT_UPLOADING: return "UPLOADING";
    case ASSISTANT_IDLE:
    default: return "IDLE";
    }
}
