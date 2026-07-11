#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef enum {
    AUDIO_LOOPBACK_OFF,
    AUDIO_LOOPBACK_STARTING,
    AUDIO_LOOPBACK_OUTPUT_TEST,
    AUDIO_LOOPBACK_RUNNING,
    AUDIO_LOOPBACK_ERROR,
} audio_loopback_status_t;

esp_err_t audio_loopback_init(void);
void audio_loopback_start(void);
void audio_loopback_stop(void);
audio_loopback_status_t audio_loopback_get_status(void);
uint8_t audio_loopback_get_level(void);
