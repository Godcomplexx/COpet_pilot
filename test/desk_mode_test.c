#include <assert.h>
#include <stdio.h>

#include "modes/desk_mode.h"

int main(void)
{
    assert(desk_mode_classify_comfort(false, 24.0f, 45.0f) ==
           DESK_COMFORT_SENSOR_ERROR);
    assert(desk_mode_classify_comfort(true, 17.9f, 45.0f) ==
           DESK_COMFORT_COLD);
    assert(desk_mode_classify_comfort(true, 24.0f, 29.9f) ==
           DESK_COMFORT_DRY);
    assert(desk_mode_classify_comfort(true, 24.0f, 45.0f) ==
           DESK_COMFORT_COMFY);
    assert(desk_mode_classify_comfort(true, 24.0f, 70.1f) ==
           DESK_COMFORT_HUMID);
    assert(desk_mode_classify_comfort(true, 28.1f, 45.0f) ==
           DESK_COMFORT_HOT);

    desk_mode_t desk;
    desk_mode_init(&desk, 1000U);
    desk_mode_set_environment(&desk, true, 23.5f, 48.0f);
    desk_mode_update(&desk, 2500U);
    assert(desk_mode_get_view(&desk)->expression == DESK_EXPRESSION_NEUTRAL);

    desk_mode_update(&desk, 32000U);
    assert(desk_mode_get_view(&desk)->expression == DESK_EXPRESSION_BORED);
    desk_mode_update(&desk, 92000U);
    assert(desk_mode_get_view(&desk)->vibe == DESK_VIBE_SMOKING);
    desk_mode_update(&desk, 602000U);
    assert(desk_mode_get_view(&desk)->expression == DESK_EXPRESSION_SLEEPY);

    desk_mode_on_touch(&desk, 603000U);
    desk_mode_update(&desk, 603000U);
    assert(desk_mode_get_view(&desk)->touch_reaction == DESK_TOUCH_HAPPY);
    assert(desk_mode_get_view(&desk)->expression == DESK_EXPRESSION_HAPPY);
    desk_mode_update(&desk, 604000U);
    assert(!desk_mode_get_view(&desk)->reacting);

    desk_mode_on_touch(&desk, 605000U);
    assert(desk_mode_get_view(&desk)->touch_reaction == DESK_TOUCH_EXCITED);
    desk_mode_on_touch(&desk, 606000U);
    desk_mode_update(&desk, 606000U);
    assert(desk_mode_get_view(&desk)->touch_reaction == DESK_TOUCH_WINK);
    assert(desk_mode_get_view(&desk)->expression == DESK_EXPRESSION_NEUTRAL);

    const desk_motion_event_t fall = desk_mode_set_motion_sample(
        &desk, true, 0.05f, 0.05f, 0.05f, 0, 0, 0, 608000U);
    assert(fall == DESK_MOTION_FALLING);
    desk_mode_update(&desk, 608000U);
    assert(desk_mode_get_view(&desk)->expression == DESK_EXPRESSION_SCARED);
    assert(desk_mode_get_view(&desk)->vibe == DESK_VIBE_SHIVER);

    desk_mode_set_environment(&desk, true, 29.0f, 45.0f);
    desk_mode_set_motion_sample(&desk, true, 0, 0, 1, 0, 0, 0, 610000U);
    desk_mode_update(&desk, 610000U);
    assert(desk_mode_get_view(&desk)->expression == DESK_EXPRESSION_SCARED);
    assert(desk_mode_get_view(&desk)->vibe == DESK_VIBE_OVERHEATED);

    puts("desk_mode_tests: OK");
    return 0;
}
