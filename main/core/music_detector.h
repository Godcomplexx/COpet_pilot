#ifndef COPET_MUSIC_DETECTOR_H
#define COPET_MUSIC_DETECTOR_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Turns a stream of microphone loudness samples (0..255) into a stable
 * "there is sustained sound around" signal, used to show the `listening`
 * emotion. This is intentionally not a music classifier: it reacts to
 * sustained ambient sound / rhythm, with smoothing + hysteresis + debounce so
 * a single clap or a brief pause does not toggle it. Pure logic, no hardware,
 * so the thresholds and timing are verified by host tests.
 */

enum {
    MUSIC_ON_LEVEL = 40,      /* smoothed level to start listening */
    MUSIC_OFF_LEVEL = 22,     /* smoothed level to drop below (hysteresis) */
    MUSIC_SUSTAIN_MS = 1500,  /* loud this long -> listening */
    MUSIC_QUIET_MS = 1200,    /* quiet this long -> stop listening */
};

typedef struct {
    uint16_t level;           /* smoothed loudness (EMA of the raw samples) */
    uint32_t timing_start_ms; /* when the pending on/off transition began */
    bool timing;              /* a transition is currently being timed */
    bool listening;           /* debounced output */
} music_detector_t;

void music_detector_init(music_detector_t *detector);

/*
 * Feed one loudness sample (0..255) with a monotonic timestamp. Returns the
 * current debounced listening state.
 */
bool music_detector_update(music_detector_t *detector, uint8_t level,
                           uint32_t now_ms);

#endif
