#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    COPET_AUDIO_MENU_MOVE,
    COPET_AUDIO_MENU_CONFIRM,
    COPET_AUDIO_VIEW_CHANGE,
    COPET_AUDIO_FOCUS_START,
    COPET_AUDIO_FOCUS_PAUSE,
    COPET_AUDIO_FOCUS_COMPLETE,
} copet_audio_event_t;

esp_err_t copet_audio_init(void);
void copet_audio_set_enabled(bool enabled);
bool copet_audio_is_enabled(void);

/* Queues one embedded PCM clip and returns immediately. */
esp_err_t copet_audio_play_event(copet_audio_event_t event);
