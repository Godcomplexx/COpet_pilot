#include "modes/assistant_mode.h"
#include "test_util.h"

static void test_preset_selection_wraps(void)
{
    assistant_mode_t a;
    assistant_mode_init(&a);
    CHECK(a.state == ASSISTANT_IDLE);
    CHECK(a.selected == 0);
    CHECK(assistant_mode_preset_count() == 3);
    CHECK(assistant_mode_selected_preset(&a) == assistant_mode_preset_at(0));

    assistant_mode_scroll(&a, 1);
    CHECK(a.selected == 1);
    assistant_mode_scroll(&a, 5); /* 1 + 5 = 6 -> wraps to 0 */
    CHECK(a.selected == 0);
    assistant_mode_scroll(&a, -1); /* wraps to last */
    CHECK(a.selected == 2);
}

static void test_submit_then_answer(void)
{
    assistant_mode_t a;
    assistant_mode_init(&a);

    const assistant_preset_t *sent = assistant_mode_submit(&a, 1000);
    CHECK(sent == assistant_mode_selected_preset(&a));
    CHECK(a.state == ASSISTANT_WAITING);

    /* A second submit while waiting is ignored (single in-flight request). */
    CHECK(assistant_mode_submit(&a, 1100) == NULL);
    CHECK(a.state == ASSISTANT_WAITING);
    /* Scrolling is frozen while not idle. */
    assistant_mode_scroll(&a, 1);
    CHECK(a.selected == 0);

    assistant_mode_on_answer(&a, "Sunny, ~18C", "helpful", 1500);
    CHECK(a.state == ASSISTANT_RESULT);
    CHECK(a.has_result);
    CHECK_STR(a.result_text, "Sunny, ~18C");
    CHECK_STR(a.result_mood, "helpful");

    /* An answer that arrives when not waiting is ignored. */
    assistant_mode_on_answer(&a, "late", "sad", 1600);
    CHECK_STR(a.result_text, "Sunny, ~18C");

    assistant_mode_back(&a);
    CHECK(a.state == ASSISTANT_IDLE);
    CHECK(!a.has_result);
    CHECK_STR(a.result_text, "");
}

static void test_error_path(void)
{
    assistant_mode_t a;
    assistant_mode_init(&a);
    assistant_mode_submit(&a, 0);
    assistant_mode_on_error(&a, "Service busy", 200);
    CHECK(a.state == ASSISTANT_ERROR);
    CHECK(!a.has_result);
    CHECK_STR(a.result_text, "Service busy");
    CHECK_STR(a.result_mood, "");
    assistant_mode_back(&a);
    CHECK(a.state == ASSISTANT_IDLE);
}

static void test_timeout(void)
{
    assistant_mode_t a;
    assistant_mode_init(&a);

    /* Tick with nothing in flight does nothing. */
    CHECK(assistant_mode_tick(&a, 5000) == false);

    assistant_mode_submit(&a, 0);
    CHECK(assistant_mode_tick(&a, ASSISTANT_TIMEOUT_MS - 1) == false);
    CHECK(a.state == ASSISTANT_WAITING);
    CHECK(assistant_mode_tick(&a, ASSISTANT_TIMEOUT_MS) == true);
    CHECK(a.state == ASSISTANT_ERROR);
    /* An answer after the timeout no longer applies. */
    assistant_mode_on_answer(&a, "too late", "helpful", ASSISTANT_TIMEOUT_MS + 1);
    CHECK(a.state == ASSISTANT_ERROR);
}

static void test_show_result_directly(void)
{
    assistant_mode_t a;
    assistant_mode_init(&a);
    /* A local skill can show a result without going through WAITING. */
    assistant_mode_show_result(&a, "IT IS 14:30", "neutral");
    CHECK(a.state == ASSISTANT_RESULT);
    CHECK(a.has_result);
    CHECK_STR(a.result_text, "IT IS 14:30");
    CHECK_STR(a.result_mood, "neutral");
    assistant_mode_back(&a);
    CHECK(a.state == ASSISTANT_IDLE);
}

static void test_state_labels(void)
{
    CHECK_STR(assistant_mode_state_label(ASSISTANT_IDLE), "IDLE");
    CHECK_STR(assistant_mode_state_label(ASSISTANT_WAITING), "WAITING");
    CHECK_STR(assistant_mode_state_label(ASSISTANT_RESULT), "RESULT");
    CHECK_STR(assistant_mode_state_label(ASSISTANT_ERROR), "ERROR");
}

int main(void)
{
    test_preset_selection_wraps();
    test_submit_then_answer();
    test_error_path();
    test_timeout();
    test_show_result_directly();
    test_state_labels();
    TEST_REPORT("assistant_mode");
}
