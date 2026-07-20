#include "core/music_detector.h"

void music_detector_init(music_detector_t *detector)
{
    detector->level = 0;
    detector->zcr = 0;
    detector->zcr_prev = 0;
    detector->zcr_dev = 0;
    for (uint8_t i = 0; i < MUSIC_ENV_RING; ++i) {
        detector->env[i] = 0;
    }
    detector->env_head = 0;
    detector->env_count = 0;
    detector->env_last_ms = 0;
    detector->env_started = false;
    detector->timing_start_ms = 0;
    detector->timing = false;
    detector->listening = false;
}

/* Push one loudness envelope sample into the ring buffer. */
static void env_push(music_detector_t *d, uint8_t value)
{
    d->env[d->env_head] = value;
    d->env_head = (uint8_t)((d->env_head + 1U) % MUSIC_ENV_RING);
    if (d->env_count < MUSIC_ENV_RING) {
        ++d->env_count;
    }
}

/* Read the ring newest-first: age 0 is the most recent sample. */
static uint8_t env_at(const music_detector_t *d, uint8_t age)
{
    const uint8_t idx =
        (uint8_t)((d->env_head + MUSIC_ENV_RING - 1U - age) % MUSIC_ENV_RING);
    return d->env[idx];
}

/*
 * Rhythm strength (0..100): scan candidate beat lags and return the strongest
 * normalized autocorrelation of the mean-removed envelope. A clear beat makes
 * the envelope self-similar at its period, producing a high peak; steady or
 * random loudness does not.
 */
static uint8_t beat_strength(const music_detector_t *d)
{
    if (d->env_count <= MUSIC_BEAT_LAG_MAX) {
        return 0;
    }

    /* Mean of the available samples, for removing the DC component. */
    int32_t sum = 0;
    for (uint8_t age = 0; age < d->env_count; ++age) {
        sum += env_at(d, age);
    }
    const int32_t mean = sum / d->env_count;

    /* Zero-lag energy normalizes the correlation into a 0..1 ratio. */
    int64_t energy = 0;
    for (uint8_t age = 0; age < d->env_count; ++age) {
        const int32_t v = (int32_t)env_at(d, age) - mean;
        energy += (int64_t)v * v;
    }
    if (energy == 0) {
        return 0;
    }

    int64_t best = 0;
    for (uint8_t lag = MUSIC_BEAT_LAG_MIN; lag <= MUSIC_BEAT_LAG_MAX; ++lag) {
        int64_t corr = 0;
        const uint8_t pairs = (uint8_t)(d->env_count - lag);
        for (uint8_t age = 0; age < pairs; ++age) {
            const int32_t a = (int32_t)env_at(d, age) - mean;
            const int32_t b = (int32_t)env_at(d, (uint8_t)(age + lag)) - mean;
            corr += (int64_t)a * b;
        }
        if (corr > best) {
            best = corr;
        }
    }
    if (best <= 0) {
        return 0;
    }

    /* Normalize the peak against the zero-lag energy, scaled to 0..100. */
    int64_t ratio = (best * 100) / energy;
    if (ratio > 100) { ratio = 100; }
    return (uint8_t)ratio;
}

/* Instantaneous melodicity: combine tonality and rhythm into 0..100. */
static uint8_t score_frame(const music_detector_t *d)
{
    if (d->level < MUSIC_MIN_LEVEL) {
        return 0;
    }

    const bool in_band =
        d->zcr >= MUSIC_ZCR_BAND_LOW && d->zcr <= MUSIC_ZCR_BAND_HIGH;
    if (!in_band) {
        return 0;
    }

    const bool steady = d->zcr_dev <= MUSIC_ZCR_STEADY_MAX;
    if (!steady) {
        return 0;
    }

    const uint8_t beat = beat_strength(d);
    const bool strong_tone = d->zcr_dev <= MUSIC_ZCR_STRONG_MAX;

    /* Tonal + a beat, or a strong sustained tone on its own. */
    if (beat >= MUSIC_BEAT_STRENGTH_ON) {
        return beat > 60 ? 100 : 75;
    }
    if (strong_tone) {
        return 60;
    }
    return 40; /* tonal and steady but no clear beat -> weak, below trigger */
}

uint8_t music_detector_score(const music_detector_t *detector)
{
    return score_frame(detector);
}

bool music_detector_update(music_detector_t *detector, uint8_t level,
                           uint8_t zcr, uint32_t now_ms)
{
    /* Smooth loudness (fast) and ZCR (fast), then track ZCR steadiness. */
    detector->level = (uint16_t)((detector->level * 3U + level) / 4U);

    /* Deviation is measured on the RAW ZCR against the running mean, not on
     * the smoothed value: broadband noise jitters frame to frame, and pre-
     * smoothing would hide exactly the jitter we use to reject it. A pitched
     * tone stays close to its mean, so its deviation stays low. */
    detector->zcr_prev = detector->zcr;
    detector->zcr = (uint16_t)((detector->zcr * 3U + zcr) / 4U);
    const int32_t zcr_change = (int32_t)zcr - (int32_t)detector->zcr;
    const uint16_t zcr_mag = (uint16_t)(zcr_change < 0 ? -zcr_change : zcr_change);
    detector->zcr_dev =
        (uint16_t)((detector->zcr_dev * 7U + zcr_mag) / 8U);

    /* Sample the envelope ring on a fixed cadence so the beat autocorrelation
     * lags map to real time regardless of the caller's frame rate. */
    if (!detector->env_started) {
        detector->env_started = true;
        detector->env_last_ms = now_ms;
        env_push(detector, (uint8_t)detector->level);
    } else {
        while ((uint32_t)(now_ms - detector->env_last_ms) >= MUSIC_ENV_STEP_MS) {
            detector->env_last_ms += MUSIC_ENV_STEP_MS;
            env_push(detector, (uint8_t)detector->level);
        }
    }

    /* A frame is musical when its melodicity score clears the trigger. */
    const bool musical = score_frame(detector) >= 50;

    /* Debounce with hysteresis: crossing the boundary starts a timer; it must
     * hold for the sustain (to start) or quiet (to stop) window to flip. */
    const bool want_flip = detector->listening ? !musical : musical;
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

    return detector->listening;
}
