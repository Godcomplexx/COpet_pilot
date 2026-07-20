#ifndef COPET_MUSIC_DETECTOR_H
#define COPET_MUSIC_DETECTOR_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Turns a stream of microphone loudness samples (0..255) into a stable "music
 * is playing" signal for the listening emotion.
 *
 * It reacts to ongoing *fluctuation*, not absolute loudness: activity is the
 * smoothed frame-to-frame change of the level (total variation). A steady
 * sound -- room hum, a fan, a mic DC offset, constant noise -- barely changes
 * between frames, so its activity stays ~0 and it is ignored no matter how
 * loud. A one-off jump (silence -> loud) is a single spike that decays inside a
 * few frames, shorter than the sustain window, so it does not trigger either.
 * Music keeps the level moving, so activity stays high and, once sustained,
 * shows the listening face. Hysteresis + debounce stop chattering. Pure logic,
 * verified by host tests.
 */

enum {
    MUSIC_MIN_LEVEL = 10,      /* ignore near-silence regardless of activity */
    MUSIC_ACTIVITY_ON = 10,    /* sustained fluctuation needed to start */
    MUSIC_ACTIVITY_OFF = 5,    /* fluctuation to keep it (hysteresis) */
    MUSIC_SUSTAIN_MS = 1200,   /* activity this long -> listening */
    MUSIC_QUIET_MS = 1500,     /* below activity this long -> stop */
};

typedef struct {
    uint16_t level;           /* fast EMA of the raw loudness */
    uint16_t previous;        /* last frame's level, for the change measure */
    uint16_t activity;        /* smoothed |level - previous| */
    uint32_t timing_start_ms; /* when the pending on/off transition began */
    bool timing;
    bool listening;
} music_detector_t;

void music_detector_init(music_detector_t *detector);

/*
 * Feed one loudness sample (0..255) with a monotonic timestamp. Returns the
 * current debounced listening state.
 */
bool music_detector_update(music_detector_t *detector, uint8_t level,
                           uint32_t now_ms);

#endif
