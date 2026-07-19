#include "modes/desk_mode.h"
#include "test_util.h"

static desk_motion_event_t classify(float ax, float ay, float az,
                                    float gx, float gy, float gz)
{
    desk_mode_t desk;
    desk_mode_init(&desk, 0);
    return desk_mode_set_motion_sample(&desk, true, ax, ay, az,
                                       gx, gy, gz, 100);
}

static void test_motion_classification(void)
{
    CHECK(classify(0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f) ==
          DESK_MOTION_NONE);
    CHECK(classify(0.0f, 0.0f, 1.20f, 0.0f, 0.0f, 0.0f) ==
          DESK_MOTION_MOVED);
    CHECK(classify(0.86f, 0.0f, 0.50f, 0.0f, 0.0f, 0.0f) ==
          DESK_MOTION_TILTED);
    CHECK(classify(0.0f, 0.0f, 1.0f, 100.0f, 80.0f, 50.0f) ==
          DESK_MOTION_SHAKEN);
    CHECK(classify(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f) ==
          DESK_MOTION_FALLING);
}

static void test_motion_labels(void)
{
    CHECK_STR(desk_mode_motion_label(DESK_MOTION_NONE), "NONE");
    CHECK_STR(desk_mode_motion_label(DESK_MOTION_MOVED), "MOVED");
    CHECK_STR(desk_mode_motion_label(DESK_MOTION_TILTED), "TILTED");
    CHECK_STR(desk_mode_motion_label(DESK_MOTION_SHAKEN), "SHAKEN");
    CHECK_STR(desk_mode_motion_label(DESK_MOTION_FALLING), "FALLING");
}

int main(void)
{
    test_motion_classification();
    test_motion_labels();
    TEST_REPORT("desk_mode");
}
