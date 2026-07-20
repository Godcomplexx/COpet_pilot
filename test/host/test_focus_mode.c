#include "modes/focus_mode.h"
#include "test_util.h"

/* Convenience: seconds -> microseconds for the tick clock. */
static int64_t us(int64_t seconds)
{
    return seconds * 1000000;
}

static void test_initial_state(void)
{
    focus_mode_t focus;
    focus_mode_init(&focus);
    CHECK(focus.state == FOCUS_TIMER_READY);
    CHECK(focus.break_phase == false);
    CHECK(focus.remaining_seconds == FOCUS_WORK_SECONDS);
    CHECK(focus.sessions == 0);
    CHECK_STR(focus_mode_status_label(&focus), "WORK READY");
    CHECK_STR(focus_mode_action_hint(&focus), "TOUCH START");
}

static void test_start_and_tick(void)
{
    focus_mode_t focus;
    focus_mode_init(&focus);

    CHECK(focus_mode_toggle(&focus, us(0)) == FOCUS_TOGGLE_STARTED);
    CHECK(focus.state == FOCUS_TIMER_RUNNING);
    CHECK_STR(focus_mode_action_hint(&focus), "TOUCH PAUSE");

    /* Sub-second progress must not consume a whole second. */
    CHECK(focus_mode_tick(&focus, 500000) == false);
    CHECK(focus.remaining_seconds == FOCUS_WORK_SECONDS);

    /* Ten seconds elapse -> ten seconds consumed, view changed. */
    CHECK(focus_mode_tick(&focus, us(10)) == true);
    CHECK(focus.remaining_seconds == FOCUS_WORK_SECONDS - 10);

    /* The tick clock is anchored, so it does not double-count. */
    CHECK(focus_mode_tick(&focus, us(10)) == false);
    CHECK(focus.remaining_seconds == FOCUS_WORK_SECONDS - 10);
}

static void test_work_completes_into_break(void)
{
    focus_mode_t focus;
    focus_mode_init(&focus);
    focus_mode_toggle(&focus, us(0));

    /* Jump past the whole work interval. */
    CHECK(focus_mode_tick(&focus, us(FOCUS_WORK_SECONDS + 5)) == true);
    CHECK(focus.break_phase == true);
    CHECK(focus.state == FOCUS_TIMER_READY);
    CHECK(focus.sessions == 1);
    CHECK(focus.remaining_seconds == FOCUS_BREAK_SECONDS);
    CHECK_STR(focus_mode_status_label(&focus), "BREAK READY");

    /* Resume the break and let it finish -> back to a fresh work phase. */
    CHECK(focus_mode_toggle(&focus, us(FOCUS_WORK_SECONDS + 5)) ==
          FOCUS_TOGGLE_STARTED);
    CHECK(focus.state == FOCUS_TIMER_RUNNING);
    CHECK(focus_mode_tick(&focus,
                          us(FOCUS_WORK_SECONDS + 5 + FOCUS_BREAK_SECONDS)) ==
          true);
    CHECK(focus.break_phase == false);
    CHECK(focus.state == FOCUS_TIMER_READY);
    CHECK(focus.sessions == 1); /* break end does not add a session */
    CHECK(focus.remaining_seconds == FOCUS_WORK_SECONDS);
}

static void test_pause_preserves_and_resumes_cleanly(void)
{
    focus_mode_t focus;
    focus_mode_init(&focus);
    focus_mode_toggle(&focus, us(0));
    focus_mode_tick(&focus, us(60));
    const uint32_t after_minute = focus.remaining_seconds;

    /* Short touch while running pauses and keeps the remaining time. */
    CHECK(focus_mode_toggle(&focus, us(60)) == FOCUS_TOGGLE_PAUSED);
    CHECK(focus.state == FOCUS_TIMER_PAUSED);
    CHECK(focus.remaining_seconds == after_minute);
    CHECK_STR(focus_mode_action_hint(&focus), "TOUCH RESUME");

    /* A tick while paused does nothing even though the clock moved on. */
    CHECK(focus_mode_tick(&focus, us(600)) == false);
    CHECK(focus.remaining_seconds == after_minute);

    /* Resuming re-anchors the clock: no time is lost from the pause. */
    CHECK(focus_mode_toggle(&focus, us(600)) == FOCUS_TOGGLE_RESUMED);
    CHECK(focus.state == FOCUS_TIMER_RUNNING);
    CHECK(focus_mode_tick(&focus, us(601)) == true);
    CHECK(focus.remaining_seconds == after_minute - 1);
}

static void test_home_pause_only_affects_running(void)
{
    focus_mode_t focus;
    focus_mode_init(&focus);

    /* READY timer left home: stays READY. */
    focus_mode_pause(&focus);
    CHECK(focus.state == FOCUS_TIMER_READY);

    /* Running timer left home: paused, remaining kept. */
    focus_mode_toggle(&focus, us(0));
    focus_mode_tick(&focus, us(30));
    const uint32_t remaining = focus.remaining_seconds;
    focus_mode_pause(&focus);
    CHECK(focus.state == FOCUS_TIMER_PAUSED);
    CHECK(focus.remaining_seconds == remaining);
}

static void test_presets(void)
{
    focus_mode_t focus;
    focus_mode_init(&focus);
    CHECK(focus.preset == 0);
    CHECK(focus_mode_work_seconds(&focus) == FOCUS_WORK_SECONDS);
    CHECK(focus_mode_break_seconds(&focus) == FOCUS_BREAK_SECONDS);
    CHECK_STR(focus_mode_preset_label(&focus), "25/5");
    CHECK(focus_mode_preset_count() >= 2);

    /* Turning while READY cycles the preset and resets the remaining time. */
    focus_mode_select_preset(&focus, 1);
    CHECK(focus.preset == 1);
    CHECK_STR(focus_mode_preset_label(&focus), "50/10");
    CHECK(focus.remaining_seconds == focus_mode_work_seconds(&focus));
    CHECK(focus.remaining_seconds == 50U * 60U);

    /* Turning backward from the first preset wraps to the last. */
    focus_mode_init(&focus);
    focus_mode_select_preset(&focus, -1);
    CHECK(focus.preset == (uint8_t)(focus_mode_preset_count() - 1U));

    /* The selected preset drives the break length after a completed work run. */
    focus_mode_init(&focus);
    focus_mode_select_preset(&focus, 2); /* 60/20 */
    CHECK_STR(focus_mode_preset_label(&focus), "60/20");
    focus_mode_toggle(&focus, us(0));
    CHECK(focus_mode_tick(&focus, us(60 * 60 + 5)) == true);
    CHECK(focus.break_phase == true);
    CHECK(focus.remaining_seconds == 20U * 60U);

    /* The preset is locked once the timer is running or paused. */
    focus_mode_init(&focus);
    focus_mode_toggle(&focus, us(0));
    focus_mode_select_preset(&focus, 1);
    CHECK(focus.preset == 0);
    focus_mode_pause(&focus);
    focus_mode_select_preset(&focus, 1);
    CHECK(focus.preset == 0);
}

int main(void)
{
    test_initial_state();
    test_presets();
    test_start_and_tick();
    test_work_completes_into_break();
    test_pause_preserves_and_resumes_cleanly();
    test_home_pause_only_affects_running();
    TEST_REPORT("focus_mode");
}
