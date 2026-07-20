#pragma once

#include <stdbool.h>
#include <stdint.h>

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

/*
 * Smoothed microphone loudness, 0..255 (0 when the mic is unavailable or
 * disabled). Updated in the background from the INMP441 on the shared I2S bus.
 */
uint8_t copet_audio_get_mic_level(void);
