#ifndef COPET_MUSIC_DETECTOR_H
#define COPET_MUSIC_DETECTOR_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Turns the microphone loudness stream into a "music is playing" signal for the
 * listening emotion.
 *
 * Loudness alone is the wrong cue: a talkative room is loud but is not music.
 * What sets music apart is a *steady beat* -- the loudness envelope rises and
 * falls periodically at the tempo. So this detector looks for rhythm, not
 * volume:
 *
 *   1. Bin the raw loudness into fixed-time frames (peak-hold per frame) to get
 *      a loudness envelope, kept in a rolling window a few seconds long.
 *   2. Take the onset envelope = positive frame-to-frame rise (the "hits").
 *   3. Autocorrelate the onset envelope over the musical tempo range and see how
 *      far the best periodic peak stands out above the average (peakiness). A
 *      real beat gives one tall peak at the beat period (and its multiples);
 *      speech / room noise autocorrelates flat.
 *   4. Require enough onset energy too, so a near-silent window cannot produce a
 *      spurious peak.
 *
 * Hysteresis + a multi-second sustain/quiet debounce keep the flag steady. The
 * `zcr` input is kept for compatibility/diagnostics only. Pure logic,
 * host-tested. The thresholds below are the tuning knobs.
 */

enum {
    MUSIC_FRAME_MS = 30,        /* envelope frame length (~33 fps) */
    MUSIC_ENV_LEN = 100,        /* rolling window = 100 frames = 3.0 s */

    /* Tempo search range in frames. 30 ms/frame:
     *   lag 11 -> 330 ms  -> ~182 BPM
     *   lag 40 -> 1200 ms -> ~50 BPM  */
    MUSIC_LAG_MIN = 11,
    MUSIC_LAG_MAX = 40,

    /* Peakiness = normalized autocorrelation coefficient r(lag)/r(0), x100
     * (0..100). A steady beat lines up most onset energy at the beat lag (high);
     * random bursts / speech align only a fraction of it (low). */
    MUSIC_BEAT_PEAKINESS = 45,  /* start listening above this */
    MUSIC_BEAT_OFF = 33,        /* stop below this (hysteresis) */

    /* Minimum summed onset energy over the window; rejects a quiet room where
     * the envelope barely moves (autocorr of noise could look "peaky"). */
    MUSIC_ONSET_FLOOR = 350,

    MUSIC_SUSTAIN_MS = 2000,    /* beat present this long -> listening */
    MUSIC_QUIET_MS = 2000,      /* beat absent this long -> stop */
};

typedef struct {
    uint8_t env[MUSIC_ENV_LEN]; /* loudness-envelope ring, newest at the end */
    uint16_t filled;            /* frames pushed so far (caps at ENV_LEN) */

    uint32_t frame_start_ms;    /* start time of the frame being accumulated */
    uint8_t frame_peak;         /* peak loudness within the current frame */
    bool have_frame;

    uint16_t peakiness;         /* last computed peak/mean x100 (diagnostic) */
    uint8_t bpm;                /* last detected tempo (diagnostic) */

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

/* Diagnostic score 0..100 derived from beat peakiness (0 = no rhythm). */
uint8_t music_detector_score(const music_detector_t *detector);

#endif
