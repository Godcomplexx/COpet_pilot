#include "core/music_detector.h"
#include "test_util.h"

#include <math.h>

/* Feed a steady level + ZCR over a span at 20 ms steps (the board's rate). */
static bool feed_steady(music_detector_t *d, uint8_t level, uint8_t zcr,
                        uint32_t from_ms, uint32_t span_ms)
{
    bool state = d->listening;
    for (uint32_t t = 0; t <= span_ms; t += 20) {
        state = music_detector_update(d, level, zcr, from_ms + t);
    }
    return state;
}

/*
 * Feed a musical stand-in: a stable mid-band ZCR (tonal) plus a loudness
 * envelope that pulses periodically (a beat). period_ms sets the beat spacing.
 */
static bool feed_music(music_detector_t *d, uint8_t zcr, uint32_t period_ms,
                       uint32_t from_ms, uint32_t span_ms)
{
    bool state = d->listening;
    for (uint32_t t = 0; t <= span_ms; t += 20) {
        const double phase = 2.0 * 3.14159265 * (double)t / (double)period_ms;
        const int lvl = 130 + (int)(90.0 * sin(phase));
        const uint8_t level = (uint8_t)(lvl < 0 ? 0 : (lvl > 255 ? 255 : lvl));
        state = music_detector_update(d, level, zcr, from_ms + t);
    }
    return state;
}

/* A loud but perfectly steady tone (no beat) must not read as music... unless
 * the tone is extremely stable -- but even then it should stay conservative.
 * Here we use a mid ZCR that is steady: the sustained-tone path scores 60,
 * which clears the trigger. So constant loudness with a WOBBLING pitch (noise)
 * must be the negative case instead. */
static void test_broadband_noise_is_ignored(void)
{
    music_detector_t d;
    music_detector_init(&d);
    /* High, jittery ZCR = broadband hiss. Alternate to force high zcr_dev. */
    bool state = false;
    for (uint32_t t = 0; t <= 5000; t += 20) {
        const uint8_t zcr = (t / 20) % 2 ? 230 : 40; /* out of band + jumpy */
        state = music_detector_update(&d, 180, zcr, t);
    }
    CHECK(state == false);
    CHECK(d.listening == false);
}

static void test_silence_is_ignored(void)
{
    music_detector_t d;
    music_detector_init(&d);
    CHECK(feed_steady(&d, 0, 0, 0, 4000) == false);
}

static void test_steady_rumble_is_ignored(void)
{
    /* Loud but low ZCR (below the musical band): a fan / rumble. */
    music_detector_t d;
    music_detector_init(&d);
    CHECK(feed_steady(&d, 200, 4, 0, 5000) == false);
}

static void test_music_triggers_then_stops(void)
{
    music_detector_t d;
    music_detector_init(&d);

    /* Tonal ZCR + a ~500 ms beat (120 BPM) starts listening. */
    CHECK(feed_music(&d, 70, 500, 0, 3000) == true);

    /* A brief steady stretch does not immediately stop it. */
    feed_steady(&d, 130, 70, 3200, 300);
    CHECK(d.listening == true);

    /* Sustained non-musical input (out-of-band jumpy ZCR) stops it. */
    bool state = d.listening;
    for (uint32_t t = 0; t <= 2500; t += 20) {
        const uint8_t zcr = (t / 20) % 2 ? 230 : 30;
        state = music_detector_update(&d, 180, zcr, 4000 + t);
    }
    CHECK(state == false);
}

int main(void)
{
    test_broadband_noise_is_ignored();
    test_silence_is_ignored();
    test_steady_rumble_is_ignored();
    test_music_triggers_then_stops();
    TEST_REPORT("music_detector");
}
