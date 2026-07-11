#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t copet_audio_init(void);

/* Queues a mono tone and returns immediately. */
esp_err_t copet_audio_play_tone(uint16_t frequency_hz, uint16_t duration_ms);

/*
 * Queues three clearly separated tones using different valid MAX98357A
 * formats. Intended only for wiring and compatibility diagnosis.
 */
esp_err_t copet_audio_run_diagnostic(void);
