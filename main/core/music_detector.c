#include "core/music_detector.h"

void music_detector_init(music_detector_t *detector)
{
    detector->level = 0;
    detector->timing_start_ms = 0;
    detector->timing = false;
    detector->listening = false;
}

bool music_detector_update(music_detector_t *detector, uint8_t level,
                           uint32_t now_ms)
{
    /* Exponential moving average smooths out transients (1/8 new sample). */
    detector->level =
        (uint16_t)((detector->level * 7U + (uint16_t)level) / 8U);

    /* When quiet, wait for a sustained loud stretch; when listening, wait for
     * a sustained quiet stretch. Hysteresis (ON vs OFF) avoids chattering. */
    const bool crossing = detector->listening
        ? detector->level < MUSIC_OFF_LEVEL
        : detector->level >= MUSIC_ON_LEVEL;

    if (crossing) {
        if (!detector->timing) {
            detector->timing = true;
            detector->timing_start_ms = now_ms;
        }
        const uint32_t needed = detector->listening
            ? MUSIC_QUIET_MS
            : MUSIC_SUSTAIN_MS;
        if ((uint32_t)(now_ms - detector->timing_start_ms) >= needed) {
            detector->listening = !detector->listening;
            detector->timing = false;
        }
    } else {
        detector->timing = false;
    }

    return detector->listening;
}
