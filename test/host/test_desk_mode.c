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
    /* Rotation/handling (high gyro, ~1g) is carrying -> MOVED, not a hit. */
    CHECK(classify(0.0f, 0.0f, 1.0f, 100.0f, 80.0f, 50.0f) ==
          DESK_MOTION_MOVED);
    /* A sharp acceleration spike (~>2g) is a hit -> SHAKEN. */
    CHECK(classify(2.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f) ==
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

/* The idle face should glance randomly within bounds -- not loop a fixed
 * pattern -- returning to centre sometimes and darting elsewhere. */
static void test_idle_gaze_is_varied_and_bounded(void)
{
    desk_mode_t desk;
    desk_mode_init(&desk, 0);

    bool bounds_ok = true;
    bool openness_ok = true;
    bool saw_center = false;
    bool saw_dart = false;
    int8_t seen_x[16];
    int distinct = 0;

    for (uint32_t t = 0; t <= 180000U; t += 100U) {
        desk_mode_update(&desk, t);
        const desk_mode_view_t *v = desk_mode_get_view(&desk);
        if (v->gaze_x < -16 || v->gaze_x > 16 ||
            v->gaze_y < -7 || v->gaze_y > 7) {
            bounds_ok = false;
        }
        if (v->eye_open_percent < 8 || v->eye_open_percent > 100) {
            openness_ok = false;
        }
        if (v->gaze_x == 0 && v->gaze_y == 0) {
            saw_center = true;
        } else {
            saw_dart = true;
            bool known = false;
            for (int i = 0; i < distinct; ++i) {
                if (seen_x[i] == v->gaze_x) { known = true; break; }
            }
            if (!known && distinct < 16) { seen_x[distinct++] = v->gaze_x; }
        }
    }

    CHECK(bounds_ok);
    CHECK(openness_ok);
    CHECK(saw_center);
    CHECK(saw_dart);
    CHECK(distinct >= 4); /* varied targets, not a single repeated value */
}

int main(void)
{
    test_motion_classification();
    test_motion_labels();
    test_idle_gaze_is_varied_and_bounded();
    TEST_REPORT("desk_mode");
}
