#include "core/music_detector.h"

void music_detector_init(music_detector_t *detector)
{
    detector->level = 0;
    detector->previous = 0;
    detector->activity = 0;
    detector->timing_start_ms = 0;
    detector->timing = false;
    detector->listening = false;
}

uint8_t music_detector_score(const music_detector_t *detector)
{
    if (detector->activity < MUSIC_MOVE_MIN) {
        return 0;
    }
    return detector->level > 100U ? 100U : (uint8_t)detector->level;
}

bool music_detector_update(music_detector_t *detector, uint8_t level,
                           uint8_t zcr, uint32_t now_ms)
{
    (void)zcr; /* diagnostics only */

    const uint16_t sample = (uint16_t)level;

    /* Liveliness = smoothed frame-to-frame movement of the raw loudness. A
     * steady tone or fan holds ~constant -> ~0; music/activity keeps it high. */
    const int32_t change = (int32_t)sample - (int32_t)detector->previous;
    detector->previous = sample;
    const uint16_t magnitude = (uint16_t)(change < 0 ? -change : change);
    detector->activity =
        (uint16_t)((detector->activity * 7U + magnitude) / 8U);

    detector->level = (uint16_t)((detector->level * 3U + sample) / 4U);

    /* Loud (with hysteresis) AND lively. */
    const uint16_t loud_threshold =
        detector->listening ? MUSIC_LOUD_OFF : MUSIC_LOUD_LEVEL;
    const bool active =
        detector->level >= loud_threshold &&
        detector->activity >= MUSIC_MOVE_MIN;

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

    return detector->listening;
}
