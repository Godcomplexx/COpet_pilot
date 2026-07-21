#ifndef COPET_MUSIC_DETECTOR_H
#define COPET_MUSIC_DETECTOR_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Turns the microphone loudness stream into a "sound worth listening to" signal
 * for the listening emotion.
 *
 * Reliably telling music from speech needs spectral analysis that this mic/bus
 * cannot give cleanly, so this is deliberately a *loud-and-lively* detector: it
 * fires on loud, changing sound (music, or loud activity nearby) and ignores a
 * quiet room, quiet talk, and steady loud noise. Two conditions must both hold,
 * sustained:
 *
 *   - LOUD:  the smoothed loudness is well above a quiet room.
 *   - LIVELY: the loudness keeps moving frame to frame (so a steady tone or a
 *             fan/hum -- loud but constant -- is rejected).
 *
 * Hysteresis + debounce keep the flag stable. The `zcr` input is kept for
 * compatibility/diagnostics only. Pure logic, host-tested. Thresholds are the
 * obvious tuning knobs for a given room and mic gain.
 */

enum {
    MUSIC_LOUD_LEVEL = 50,     /* smoothed level to count as "loud" (start) */
    MUSIC_LOUD_OFF = 30,       /* drop below this to stop (hysteresis) */
    MUSIC_MOVE_MIN = 5,        /* min loudness movement to count as "lively" */
    MUSIC_SUSTAIN_MS = 1500,   /* loud+lively this long -> listening */
    MUSIC_QUIET_MS = 1500,     /* not loud+lively this long -> stop */
};

typedef struct {
    uint16_t level;            /* fast EMA of the raw loudness */
    uint16_t previous;         /* previous raw loudness sample */
    uint16_t activity;         /* smoothed |sample - previous| (liveliness) */
    uint32_t timing_start_ms;
    bool timing;
    bool listening;
} music_detector_t;

void music_detector_init(music_detector_t *detector);

/*
 * Feed one frame: loudness level (0..255) and zero-crossing rate (0..255,
 * diagnostics only), with a monotonic timestamp. Returns the debounced state.
 */
bool music_detector_update(music_detector_t *detector, uint8_t level,
                           uint8_t zcr, uint32_t now_ms);

/* Diagnostic score 0..100: the smoothed loudness (0 unless also lively). */
uint8_t music_detector_score(const music_detector_t *detector);

#endif
