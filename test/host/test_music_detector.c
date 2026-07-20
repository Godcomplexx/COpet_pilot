#include "core/music_detector.h"
#include "test_util.h"

/* Feed a constant level over a span at 50 ms steps. */
static bool feed_constant(music_detector_t *d, uint8_t level,
                          uint32_t from_ms, uint32_t span_ms)
{
    bool state = d->listening;
    for (uint32_t t = 0; t <= span_ms; t += 50) {
        state = music_detector_update(d, level, from_ms + t);
    }
    return state;
}

/* Feed an alternating loud/quiet pattern (stand-in for music's moving level). */
static bool feed_music(music_detector_t *d, uint32_t from_ms, uint32_t span_ms)
{
    bool state = d->listening;
    uint32_t i = 0;
    for (uint32_t t = 0; t <= span_ms; t += 50) {
        const uint8_t level = (i++ % 2) ? 200 : 40;
        state = music_detector_update(d, level, from_ms + t);
    }
    return state;
}

static void test_constant_loud_is_ignored(void)
{
    /* The key fix: a loud but STEADY sound must never trigger listening. */
    music_detector_t d;
    music_detector_init(&d);
    CHECK(feed_constant(&d, 200, 0, 5000) == false);
    CHECK(d.listening == false);
}

static void test_silence_is_ignored(void)
{
    music_detector_t d;
    music_detector_init(&d);
    CHECK(feed_constant(&d, 0, 0, 4000) == false);
}

static void test_music_triggers_then_stops(void)
{
    music_detector_t d;
    music_detector_init(&d);

    /* Sustained fluctuation starts listening. */
    CHECK(feed_music(&d, 0, 2500) == true);

    /* A brief pause does not stop it. */
    feed_constant(&d, 120, 3000, 300);
    CHECK(d.listening == true);

    /* Sustained steady level (fluctuation gone) stops it. */
    CHECK(feed_constant(&d, 120, 4000, 2500) == false);
}

int main(void)
{
    test_constant_loud_is_ignored();
    test_silence_is_ignored();
    test_music_triggers_then_stops();
    TEST_REPORT("music_detector");
}
