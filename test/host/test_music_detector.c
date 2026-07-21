#include "core/music_detector.h"
#include "test_util.h"

/* Feed a constant loudness for span_ms (samples every 10 ms). */
static bool feed_constant(music_detector_t *d, uint8_t level,
                          uint32_t from_ms, uint32_t span_ms)
{
    bool state = d->listening;
    for (uint32_t t = 0; t <= span_ms; t += 10) {
        state = music_detector_update(d, level, 0, from_ms + t);
    }
    return state;
}

/* Feed a steady beat: a short loud pulse every period_ms, quiet in between. */
static bool feed_beat(music_detector_t *d, uint32_t from_ms, uint32_t span_ms,
                      uint32_t period_ms)
{
    bool state = d->listening;
    for (uint32_t t = 0; t <= span_ms; t += 10) {
        const uint8_t level = (t % period_ms) < 60U ? 200 : 6;
        state = music_detector_update(d, level, 0, from_ms + t);
    }
    return state;
}

/* Feed irregular loud pulses (speech / random room bursts, no steady tempo). */
static bool feed_aperiodic(music_detector_t *d, uint32_t from_ms,
                           uint32_t span_ms)
{
    static const uint32_t gaps[] = {230, 610, 340, 780, 450, 290,
                                    700, 520, 380, 640, 210, 560};
    bool state = d->listening;
    uint32_t next = 0;
    size_t g = 0;
    for (uint32_t t = 0; t <= span_ms; t += 10) {
        const bool in_pulse = (t >= next) && (t < next + 60U);
        if (t >= next + 60U) {
            next += gaps[g % (sizeof(gaps) / sizeof(gaps[0]))] + 60U;
            g++;
        }
        state = music_detector_update(d, in_pulse ? 200 : 6, 0, from_ms + t);
    }
    return state;
}

static void test_quiet_room_is_ignored(void)
{
    music_detector_t d;
    music_detector_init(&d);
    CHECK(feed_constant(&d, 6, 0, 5000) == false);
}

static void test_steady_loud_is_ignored(void)
{
    /* Loud but constant (a fan/hum) has no beat -> ignored. */
    music_detector_t d;
    music_detector_init(&d);
    CHECK(feed_constant(&d, 200, 0, 6000) == false);
}

static void test_aperiodic_is_ignored(void)
{
    /* Loud, lively, but no steady tempo (talk / random bursts) -> ignored. */
    music_detector_t d;
    music_detector_init(&d);
    CHECK(feed_aperiodic(&d, 0, 8000) == false);
}

static void test_beat_triggers_then_stops(void)
{
    music_detector_t d;
    music_detector_init(&d);
    /* 500 ms period = 120 BPM. Needs the 3 s window to fill then sustain. */
    CHECK(feed_beat(&d, 0, 6000, 500) == true);
    /* Beat stops -> flag drops after the window flushes and quiet debounce. */
    CHECK(feed_constant(&d, 6, 6000, 6000) == false);
}

int main(void)
{
    test_quiet_room_is_ignored();
    test_steady_loud_is_ignored();
    test_aperiodic_is_ignored();
    test_beat_triggers_then_stops();
    TEST_REPORT("music_detector");
}
