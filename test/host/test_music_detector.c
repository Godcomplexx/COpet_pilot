#include "core/music_detector.h"
#include "test_util.h"

static bool feed_constant(music_detector_t *d, uint8_t level,
                          uint32_t from_ms, uint32_t span_ms)
{
    bool state = d->listening;
    for (uint32_t t = 0; t <= span_ms; t += 50) {
        state = music_detector_update(d, level, 0, from_ms + t);
    }
    return state;
}

/* Loud AND changing (music / loud lively activity). */
static bool feed_loud_lively(music_detector_t *d, uint32_t from_ms,
                             uint32_t span_ms)
{
    bool state = d->listening;
    uint32_t i = 0;
    for (uint32_t t = 0; t <= span_ms; t += 50) {
        const uint8_t level = (i++ % 2) ? 120 : 70; /* loud, moving */
        state = music_detector_update(d, level, 0, from_ms + t);
    }
    return state;
}

static void test_quiet_room_is_ignored(void)
{
    music_detector_t d;
    music_detector_init(&d);
    CHECK(feed_constant(&d, 8, 0, 4000) == false);   /* quiet, steady */
}

static void test_steady_loud_is_ignored(void)
{
    /* Loud but constant (a fan/hum) is not lively -> ignored. */
    music_detector_t d;
    music_detector_init(&d);
    CHECK(feed_constant(&d, 200, 0, 5000) == false);
}

static void test_quiet_lively_is_ignored(void)
{
    /* Quiet but moving (distant talk) is not loud enough -> ignored. */
    music_detector_t d;
    music_detector_init(&d);
    bool state = false;
    for (uint32_t t = 0; t <= 4000; t += 50) {
        const uint8_t level = ((t / 50) % 2) ? 20 : 4;
        state = music_detector_update(&d, level, 0, t);
    }
    CHECK(state == false);
}

static void test_loud_lively_triggers_then_stops(void)
{
    music_detector_t d;
    music_detector_init(&d);
    CHECK(feed_loud_lively(&d, 0, 3000) == true);
    /* Goes quiet -> stops after the quiet window. */
    CHECK(feed_constant(&d, 4, 4000, 2500) == false);
}

int main(void)
{
    test_quiet_room_is_ignored();
    test_steady_loud_is_ignored();
    test_quiet_lively_is_ignored();
    test_loud_lively_triggers_then_stops();
    TEST_REPORT("music_detector");
}
