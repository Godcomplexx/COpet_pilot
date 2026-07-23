#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "core/copet_speech.h"

typedef enum {
    COPET_AUDIO_MENU_MOVE,
    COPET_AUDIO_MENU_CONFIRM,
    COPET_AUDIO_VIEW_CHANGE,
    COPET_AUDIO_FOCUS_START,
    COPET_AUDIO_FOCUS_PAUSE,
    COPET_AUDIO_FOCUS_COMPLETE,
    COPET_AUDIO_ANGRY,
    COPET_AUDIO_ASSISTANT_SPEAK, /* synthesized robot babble */
    COPET_AUDIO_SAY_PHRASE,      /* concatenated word clips (real speech) */
} copet_audio_event_t;

esp_err_t copet_audio_init(void);
void copet_audio_set_enabled(bool enabled);
bool copet_audio_is_enabled(void);

/* Queues one embedded PCM clip and returns immediately. */
esp_err_t copet_audio_play_event(copet_audio_event_t event);

/*
 * Play a short synthesized "robot voice": `syllables` pitched blips (clamped to
 * a sane range), so CoPet seems to speak an answer. This is a stylized cue, not
 * text-to-speech -- real spoken words need the cloud backend. Returns
 * immediately; respects the sound on/off setting.
 */
esp_err_t copet_audio_speak(uint32_t syllables);

/*
 * Speak a phrase built from the concatenative vocabulary (see copet_speech):
 * plays each word's embedded 16 kHz clip back to back at the bus rate.
 * Real spoken words, assembled on-device. Returns immediately; respects the
 * sound on/off setting.
 */
esp_err_t copet_audio_say(const speech_word_t *words, int count);

/*
 * Smoothed microphone loudness, 0..255 (0 when the mic is unavailable or
 * disabled). Updated in the background from the INMP441 on the shared I2S bus.
 */
uint8_t copet_audio_get_mic_level(void);

/*
 * Zero-crossing rate of the most recent mic window, normalized to 0..255
 * (crossings per window scaled up). A rough pitch proxy: near-silence and
 * low tones cross rarely (low ZCR); broadband hiss/noise crosses often (high
 * ZCR); tonal/musical content sits in a stable mid range. 0 when the mic is
 * unavailable or disabled. Read together with copet_audio_get_mic_level().
 */
uint8_t copet_audio_get_mic_zcr(void);
