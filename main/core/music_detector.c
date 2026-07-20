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

bool music_detector_update(music_detector_t *detector, uint8_t level,
                           uint32_t now_ms)
{
    const uint16_t sample = (uint16_t)level;
    detector->level = (uint16_t)((detector->level * 3U + sample) / 4U);

    /* Activity = smoothed frame-to-frame change. Steady sound -> ~0; music
     * keeps moving the level -> stays high; a single jump decays fast. */
    const int32_t change = (int32_t)detector->level - (int32_t)detector->previous;
    detector->previous = detector->level;
    const uint16_t magnitude = (uint16_t)(change < 0 ? -change : change);
    detector->activity =
        (uint16_t)((detector->activity * 7U + magnitude) / 8U);

    const uint16_t threshold =
        detector->listening ? MUSIC_ACTIVITY_OFF : MUSIC_ACTIVITY_ON;
    const bool active =
        detector->level >= MUSIC_MIN_LEVEL && detector->activity >= threshold;

    const bool crossing = detector->listening ? !active : active;
    if (crossing) {
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
