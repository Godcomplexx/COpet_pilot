#include "modes/animation_mode.h"
#include "test_util.h"

static void test_start(void)
{
    animation_mode_t animation;
    animation_mode_start(&animation, 1000);
    CHECK(animation.frame == 0);
    CHECK(animation.next_frame_us == 1000 + ANIMATION_FRAME_INTERVAL_US);
}

static void test_tick_respects_interval(void)
{
    animation_mode_t animation;
    animation_mode_start(&animation, 0);

    /* Before the interval elapses nothing changes. */
    CHECK(animation_mode_tick(&animation, ANIMATION_FRAME_INTERVAL_US - 1) ==
          false);
    CHECK(animation.frame == 0);

    /* At the interval the frame advances and the next tick is rescheduled. */
    CHECK(animation_mode_tick(&animation, ANIMATION_FRAME_INTERVAL_US) == true);
    CHECK(animation.frame == 1);
    CHECK(animation.next_frame_us == 2 * ANIMATION_FRAME_INTERVAL_US);
}

static void test_frame_wraps(void)
{
    animation_mode_t animation;
    animation_mode_start(&animation, 0);

    int64_t clock = 0;
    for (int step = 1; step < ANIMATION_FRAME_COUNT; ++step) {
        clock += ANIMATION_FRAME_INTERVAL_US;
        CHECK(animation_mode_tick(&animation, clock) == true);
        CHECK(animation.frame == (size_t)step);
    }

    /* One more advance wraps back to the first frame. */
    clock += ANIMATION_FRAME_INTERVAL_US;
    CHECK(animation_mode_tick(&animation, clock) == true);
    CHECK(animation.frame == 0);
}

int main(void)
{
    test_start();
    test_tick_respects_interval();
    test_frame_wraps();
    TEST_REPORT("animation_mode");
}
