#include "core/music_detector.h"

void music_detector_init(music_detector_t *detector)
{
    for (int i = 0; i < MUSIC_ENV_LEN; ++i) {
        detector->env[i] = 0;
    }
    detector->filled = 0;
    detector->frame_start_ms = 0;
    detector->frame_peak = 0;
    detector->have_frame = false;
    detector->peakiness = 0;
    detector->bpm = 0;
    detector->timing_start_ms = 0;
    detector->timing = false;
    detector->listening = false;
}

uint8_t music_detector_score(const music_detector_t *detector)
{
    /* peakiness is already the 0..100 autocorrelation coefficient. */
    return detector->peakiness > 100U ? 100U : (uint8_t)detector->peakiness;
}

/* Push one closed envelope frame into the rolling window (newest at the end). */
static void push_frame(music_detector_t *detector, uint8_t value)
{
    for (int i = 0; i < MUSIC_ENV_LEN - 1; ++i) {
        detector->env[i] = detector->env[i + 1];
    }
    detector->env[MUSIC_ENV_LEN - 1] = value;
    if (detector->filled < MUSIC_ENV_LEN) {
        detector->filled++;
    }
}

/* Look for a periodic beat in the onset envelope. Returns true if the rhythm is
 * strong enough (also updates peakiness/bpm diagnostics). */
static bool has_beat(music_detector_t *detector)
{
    if (detector->filled < MUSIC_ENV_LEN) {
        return false; /* window not full yet */
    }

    /* Onset envelope: positive frame-to-frame rise = a "hit". */
    uint8_t onset[MUSIC_ENV_LEN];
    onset[0] = 0;
    uint32_t onset_energy = 0;
    for (int i = 1; i < MUSIC_ENV_LEN; ++i) {
        const int diff = (int)detector->env[i] - (int)detector->env[i - 1];
        onset[i] = diff > 0 ? (uint8_t)diff : 0;
        onset_energy += onset[i];
    }

    /* Zero-lag energy over the same index range, for normalization. A periodic
     * beat lines up most of this energy at the beat lag; a lucky alignment of
     * random bursts is only a small fraction of it. */
    uint32_t energy0 = 0;
    for (int i = MUSIC_LAG_MAX; i < MUSIC_ENV_LEN; ++i) {
        energy0 += (uint32_t)onset[i] * (uint32_t)onset[i];
    }

    /* Autocorrelate the onset envelope across the tempo range. */
    uint32_t best = 0;
    int best_lag = 0;
    for (int lag = MUSIC_LAG_MIN; lag <= MUSIC_LAG_MAX; ++lag) {
        uint32_t r = 0;
        for (int i = MUSIC_LAG_MAX; i < MUSIC_ENV_LEN; ++i) {
            r += (uint32_t)onset[i] * (uint32_t)onset[i - lag];
        }
        if (r > best) {
            best = r;
            best_lag = lag;
        }
    }

    /* Normalized autocorrelation coefficient x100 (0..100). */
    detector->peakiness =
        energy0 > 0 ? (uint16_t)((best * 100U) / energy0) : 0;
    detector->bpm =
        best_lag > 0 ? (uint8_t)(60000U / ((uint32_t)best_lag * MUSIC_FRAME_MS))
                     : 0;

    if (onset_energy < (uint32_t)MUSIC_ONSET_FLOOR) {
        return false; /* too little going on to be music */
    }

    const uint16_t threshold =
        detector->listening ? MUSIC_BEAT_OFF : MUSIC_BEAT_PEAKINESS;
    return detector->peakiness >= threshold;
}

bool music_detector_update(music_detector_t *detector, uint8_t level,
                           uint8_t zcr, uint32_t now_ms)
{
    (void)zcr; /* diagnostics only */

    /* Accumulate the current frame with peak-hold so a short beat hit between
     * samples is not missed. */
    if (!detector->have_frame) {
        detector->have_frame = true;
        detector->frame_start_ms = now_ms;
        detector->frame_peak = level;
    } else if (level > detector->frame_peak) {
        detector->frame_peak = level;
    }

    /* Close frames whose time window has elapsed (usually one per call). */
    bool pushed = false;
    while ((uint32_t)(now_ms - detector->frame_start_ms) >=
           (uint32_t)MUSIC_FRAME_MS) {
        push_frame(detector, detector->frame_peak);
        detector->frame_start_ms += MUSIC_FRAME_MS;
        detector->frame_peak = level;
        pushed = true;
    }

    if (pushed) {
        const bool active = has_beat(detector);
        const bool want_flip = detector->listening ? !active : active;
        if (want_flip) {
            if (!detector->timing) {
                detector->timing = true;
                detector->timing_start_ms = now_ms;
            }
            const uint32_t needed =
                detector->listening ? MUSIC_QUIET_MS : MUSIC_SUSTAIN_MS;
            if ((uint32_t)(now_ms - detector->timing_start_ms) >= needed) {
                detector->listening = !detector->listening;
                detector->timing = false;
            }
        } else {
            detector->timing = false;
        }
    }

    return detector->listening;
}
