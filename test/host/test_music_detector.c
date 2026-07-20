#include "core/music_detector.h"
#include "test_util.h"

/* Feed a constant level for a span of ms at 50 ms steps; return final state. */
static bool feed(music_detector_t *d, uint8_t level, uint32_t from_ms,
                 uint32_t span_ms)
{
    bool state = d->listening;
    for (uint32_t t = 0; t <= span_ms; t += 50) {
        state = music_detector_update(d, level, from_ms + t);
    }
    return state;
}

static void test_starts_after_sustained_sound(void)
{
    music_detector_t d;
    music_detector_init(&d);
    CHECK(d.listening == false);

    /* A brief loud blip does not start listening. */
    music_detector_update(&d, 200, 100);
    music_detector_update(&d, 200, 200);
    CHECK(d.listening == false);

    /* Sustained loudness for > 1.5 s starts it. */
    CHECK(feed(&d, 200, 1000, 2000) == true);
}

static void test_stops_after_sustained_quiet(void)
{
    music_detector_t d;
    music_detector_init(&d);
    feed(&d, 200, 0, 2000);
    CHECK(d.listening == true);

    /* A short gap does not stop it. */
    music_detector_update(&d, 0, 3000);
    music_detector_update(&d, 0, 3200);
    CHECK(d.listening == true);

    /* Sustained quiet for > 1.2 s stops it. */
    CHECK(feed(&d, 0, 4000, 2000) == false);
}

static void test_moderate_noise_below_threshold(void)
{
    music_detector_t d;
    music_detector_init(&d);
    /* Low ambient noise never crosses the ON threshold. */
    CHECK(feed(&d, 15, 0, 4000) == false);
}

int main(void)
{
    test_starts_after_sustained_sound();
    test_stops_after_sustained_quiet();
    test_moderate_noise_below_threshold();
    TEST_REPORT("music_detector");
}
