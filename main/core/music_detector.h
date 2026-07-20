#ifndef COPET_MUSIC_DETECTOR_H
#define COPET_MUSIC_DETECTOR_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Turns a stream of microphone features into a stable "music is playing"
 * signal for the listening emotion.
 *
 * Loudness alone cannot tell music from a fan, speech, or a knock, so this
 * detector scores *melodicity* from two cheap, raw-sample features fed each
 * frame:
 *
 *   - level (0..255): smoothed loudness of the window (the loudness envelope).
 *   - zcr   (0..255): zero-crossing rate of the window, a rough pitch proxy.
 *
 * Two independent tests must agree before we call it music:
 *
 *   1. Tonality. Musical/tonal content keeps the ZCR in a stable mid band:
 *      not near-zero (silence/rumble) and not pinned high (broadband hiss),
 *      and -- crucially -- steady frame to frame. Noise and clatter push the
 *      ZCR high and make it jump around; a pure steady tone sits low and flat.
 *      We require the ZCR to sit in the musical band AND vary little (low
 *      running deviation).
 *
 *   2. Rhythm. Music modulates its loudness periodically (a beat). We keep a
 *      short ring of the loudness envelope and look for periodicity via
 *      autocorrelation at lags matching ~60..180 BPM. A clear autocorrelation
 *      peak means a beat is present; steady noise and speech do not produce
 *      one.
 *
 * A frame is "musical" when tonality holds AND (rhythm is present OR tonality
 * is strong on its own -- e.g. a sustained melodic line without a hard beat).
 * Hysteresis + a sustain/quiet debounce then stop the listening flag from
 * chattering. Pure logic, verified by host tests.
 */

enum {
    MUSIC_MIN_LEVEL = 10,      /* ignore near-silence regardless of features */

    /* Tonality: ZCR must sit inside this band to look pitched/musical. */
    MUSIC_ZCR_BAND_LOW = 12,   /* below -> rumble/hum/DC, not musical */
    MUSIC_ZCR_BAND_HIGH = 170, /* above -> broadband hiss/noise, not musical */
    MUSIC_ZCR_STEADY_MAX = 40, /* max smoothed |zcr change| to count as steady */
    MUSIC_ZCR_STRONG_MAX = 18, /* very steady tone qualifies without a beat */

    /* Rhythm: the envelope ring is sampled on a fixed cadence (see
     * MUSIC_ENV_STEP_MS) so the beat-lag math is independent of how often
     * the caller feeds frames. At 50 ms/slot, lags 6..20 cover one beat every
     * 0.30..1.0 s -> ~60..200 BPM. RING must exceed BEAT_LAG_MAX. */
    MUSIC_ENV_STEP_MS = 50,
    MUSIC_ENV_RING = 32,
    MUSIC_BEAT_LAG_MIN = 6,
    MUSIC_BEAT_LAG_MAX = 20,
    MUSIC_BEAT_STRENGTH_ON = 45, /* normalized autocorr peak (0..100) for beat */

    /* Debounce windows for the listening flag. */
    MUSIC_SUSTAIN_MS = 1200,   /* musical this long -> listening */
    MUSIC_QUIET_MS = 1500,     /* non-musical this long -> stop */
};

typedef struct {
    /* Loudness / level tracking. */
    uint16_t level;            /* fast EMA of the raw loudness */

    /* Tonality tracking (ZCR). */
    uint16_t zcr;              /* EMA of the raw zero-crossing rate */
    uint16_t zcr_prev;         /* previous smoothed ZCR, for the deviation */
    uint16_t zcr_dev;          /* EMA of |zcr - zcr_prev| (steadiness) */

    /* Rhythm tracking: ring buffer of the loudness envelope, sampled on a
     * fixed MUSIC_ENV_STEP_MS cadence. */
    uint8_t env[MUSIC_ENV_RING];
    uint8_t env_head;          /* next write position */
    uint8_t env_count;         /* valid samples in the ring (<= MUSIC_ENV_RING) */
    uint32_t env_last_ms;      /* timestamp of the last envelope push */
    bool env_started;          /* env_last_ms has been initialized */

    /* Debounced output. */
    uint32_t timing_start_ms;  /* when the pending on/off transition began */
    bool timing;
    bool listening;
} music_detector_t;

void music_detector_init(music_detector_t *detector);

/*
 * Feed one frame: loudness level (0..255) and zero-crossing rate (0..255),
 * both from copet_audio, with a monotonic timestamp. Returns the current
 * debounced listening state.
 */
bool music_detector_update(music_detector_t *detector, uint8_t level,
                           uint8_t zcr, uint32_t now_ms);

/*
 * Instantaneous melodicity score (0..100) of the most recent frame, before
 * debouncing. Exposed for tuning/diagnostics and host tests.
 */
uint8_t music_detector_score(const music_detector_t *detector);

#endif
