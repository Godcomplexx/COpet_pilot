#include "core/copet_behavior.h"
#include "test_util.h"

#include <stdint.h>

static copet_behavior_context_t desk_context(void)
{
    const copet_behavior_context_t context = {.desk_active = true};
    return context;
}

static copet_behavior_event_t event(copet_behavior_event_type_t type,
                                    int32_t value)
{
    const copet_behavior_event_t result = {.type = type, .value = value};
    return result;
}

static copet_behavior_id_t update_at(copet_behavior_t *engine,
                                     bool desk_active,
                                     uint32_t now_ms)
{
    const copet_behavior_context_t context = {
        .desk_active = desk_active,
    };
    copet_behavior_update(engine, &context, now_ms);
    return copet_behavior_get_view(engine)->id;
}

static void test_boot_lifecycle(void)
{
    copet_behavior_t engine;
    copet_behavior_init(&engine, 0, 1);
    const copet_behavior_context_t context = desk_context();
    copet_behavior_update(&engine, &context, 1000);
    const copet_behavior_event_t boot = event(
        COPET_BEHAVIOR_EVENT_BOOT_COMPLETED, 0);
    copet_behavior_post(&engine, &boot, 1000);

    CHECK(update_at(&engine, true, 2599) == COPET_BEHAVIOR_ALERT);
    CHECK(update_at(&engine, true, 2600) == COPET_BEHAVIOR_NEUTRAL);
}

static void test_priority_and_interruption(void)
{
    copet_behavior_t engine;
    copet_behavior_init(&engine, 0, 2);
    update_at(&engine, true, 0);
    engine.next_p3_ms = 0;
    const copet_behavior_id_t p3 = update_at(&engine, true, 1);
    CHECK(p3 == COPET_BEHAVIOR_ZEN || p3 == COPET_BEHAVIOR_DICE_ROLL);

    const copet_behavior_event_t fall = event(
        COPET_BEHAVIOR_EVENT_MOTION_FALLING, 0);
    copet_behavior_post(&engine, &fall, 10);
    CHECK(update_at(&engine, true, 10) == COPET_BEHAVIOR_LEGACY_SCARED);
    CHECK(copet_behavior_get_view(&engine)->priority ==
          COPET_BEHAVIOR_PRIORITY_P0);

    const copet_behavior_event_t confirm = event(
        COPET_BEHAVIOR_EVENT_CONFIRM, 0);
    copet_behavior_post(&engine, &confirm, 20);
    CHECK(update_at(&engine, true, 20) == COPET_BEHAVIOR_LEGACY_SCARED);
    const copet_behavior_event_t shake = event(
        COPET_BEHAVIOR_EVENT_MOTION_SHAKEN_STRONG, 0);
    copet_behavior_post(&engine, &shake, 30);
    CHECK(update_at(&engine, true, 30) == COPET_BEHAVIOR_LEGACY_SCARED);
    CHECK(copet_behavior_get_view(&engine)->priority ==
          COPET_BEHAVIOR_PRIORITY_P0);
    CHECK(update_at(&engine, true, 1610) == COPET_BEHAVIOR_NEUTRAL);
}

static void test_equal_priority_and_wifi_deadline(void)
{
    copet_behavior_t engine;
    copet_behavior_init(&engine, 0, 3);
    update_at(&engine, true, 0);
    const copet_behavior_event_t wifi_on = event(
        COPET_BEHAVIOR_EVENT_WIFI_CHANGED, 1);
    copet_behavior_post(&engine, &wifi_on, 100);
    CHECK(update_at(&engine, true, 100) == COPET_BEHAVIOR_CONNECTING);

    /* A repeated connecting status is level-triggered and keeps 100 ms start. */
    copet_behavior_post(&engine, &wifi_on, 500);
    const copet_behavior_event_t confirm = event(
        COPET_BEHAVIOR_EVENT_CONFIRM, 0);
    copet_behavior_post(&engine, &confirm, 500);
    CHECK(update_at(&engine, true, 500) == COPET_BEHAVIOR_ACKNOWLEDGE);
    CHECK(update_at(&engine, true, 950) == COPET_BEHAVIOR_CONNECTING);
    CHECK(update_at(&engine, true, 10100) == COPET_BEHAVIOR_NEUTRAL);
}

static void test_touch_followup(void)
{
    copet_behavior_t engine;
    copet_behavior_init(&engine, 0, 4);
    update_at(&engine, true, 0);
    const copet_behavior_event_t touch = event(
        COPET_BEHAVIOR_EVENT_TOUCH_SHORT, 0);
    copet_behavior_post(&engine, &touch, 30000);
    CHECK(update_at(&engine, true, 30000) == COPET_BEHAVIOR_DOUBLE_BLINK);
    CHECK(update_at(&engine, true, 30480) == COPET_BEHAVIOR_ATTENTIVE);
    CHECK(update_at(&engine, true, 33480) == COPET_BEHAVIOR_NEUTRAL);
}

static void test_focus_source(void)
{
    copet_behavior_t engine;
    copet_behavior_init(&engine, 0, 5);
    update_at(&engine, false, 0);
    copet_behavior_event_t focus = event(
        COPET_BEHAVIOR_EVENT_FOCUS_CHANGED,
        COPET_BEHAVIOR_FOCUS_RUNNING_WORK);
    copet_behavior_post(&engine, &focus, 100);
    CHECK(update_at(&engine, false, 100) == COPET_BEHAVIOR_FOCUSED);

    const copet_behavior_event_t confirm = event(
        COPET_BEHAVIOR_EVENT_CONFIRM, 0);
    copet_behavior_post(&engine, &confirm, 100);
    CHECK(update_at(&engine, false, 100) == COPET_BEHAVIOR_ACKNOWLEDGE);
    CHECK(update_at(&engine, false, 550) == COPET_BEHAVIOR_FOCUSED);

    focus.value = COPET_BEHAVIOR_FOCUS_PAUSED_WORK;
    copet_behavior_post(&engine, &focus, 600);
    CHECK(update_at(&engine, false, 600) == COPET_BEHAVIOR_NEUTRAL);
}

static void test_hit_is_angry(void)
{
    copet_behavior_t engine;
    copet_behavior_init(&engine, 0, 6);
    update_at(&engine, true, 0);

    /* A hit/knock goes straight to angry (P1), no scared-first flip-flop. */
    const copet_behavior_event_t shake = event(
        COPET_BEHAVIOR_EVENT_MOTION_SHAKEN_STRONG, 0);
    copet_behavior_post(&engine, &shake, 100);
    CHECK(update_at(&engine, true, 100) == COPET_BEHAVIOR_ANGRY);
    CHECK(copet_behavior_get_view(&engine)->priority ==
          COPET_BEHAVIOR_PRIORITY_P1);
    CHECK(update_at(&engine, true, 1900) == COPET_BEHAVIOR_NEUTRAL);

    /* Falling still wins as scared P0 over a hit. */
    const copet_behavior_event_t fall = event(
        COPET_BEHAVIOR_EVENT_MOTION_FALLING, 0);
    copet_behavior_post(&engine, &fall, 2000);
    CHECK(update_at(&engine, true, 2000) == COPET_BEHAVIOR_LEGACY_SCARED);
    copet_behavior_post(&engine, &shake, 2100);
    CHECK(update_at(&engine, true, 2100) == COPET_BEHAVIOR_LEGACY_SCARED);
}

static void test_p3_cooldown_history_and_cancel(void)
{
    copet_behavior_t engine;
    copet_behavior_init(&engine, 0, 7);
    update_at(&engine, true, 0);
    engine.next_p3_ms = 1;
    const copet_behavior_id_t first = update_at(&engine, true, 1);
    CHECK(first == COPET_BEHAVIOR_ZEN || first == COPET_BEHAVIOR_DICE_ROLL);
    const uint32_t first_end = 1 +
        (first == COPET_BEHAVIOR_ZEN ? 12000U : 8000U);
    update_at(&engine, true, first_end);

    engine.next_p3_ms = first_end + 1U;
    const copet_behavior_id_t second = update_at(
        &engine, true, first_end + 1U);
    CHECK(second != first);
    CHECK(second == COPET_BEHAVIOR_ZEN ||
          second == COPET_BEHAVIOR_DICE_ROLL);

    const uint32_t second_end = first_end + 1U +
        (second == COPET_BEHAVIOR_ZEN ? 12000U : 8000U);
    update_at(&engine, true, second_end);
    engine.next_p3_ms = second_end + 1U;
    CHECK(update_at(&engine, true, second_end + 1U) ==
          COPET_BEHAVIOR_NEUTRAL);

    /* After the older candidate cools down, it becomes eligible again. */
    const uint32_t cooled = first_end + 10U * 60U * 1000U;
    engine.next_p3_ms = cooled;
    CHECK(update_at(&engine, true, cooled) == first);

    const copet_behavior_event_t activity = event(
        COPET_BEHAVIOR_EVENT_USER_ACTIVITY, 0);
    copet_behavior_post(&engine, &activity, cooled + 1500U);
    CHECK(update_at(&engine, true, cooled + 1500U) ==
          COPET_BEHAVIOR_NEUTRAL);
    CHECK(update_at(&engine, true, cooled + 91499U) ==
          COPET_BEHAVIOR_NEUTRAL);
}

static copet_behavior_id_t update_pet(copet_behavior_t *engine,
                                     uint32_t hold_ms, uint32_t now_ms)
{
    const copet_behavior_context_t context = {
        .desk_active = true,
        .touch_hold_ms = hold_ms,
    };
    copet_behavior_update(engine, &context, now_ms);
    return copet_behavior_get_view(engine)->id;
}

static void test_tap_is_attentive(void)
{
    copet_behavior_t engine;
    copet_behavior_init(&engine, 0, 9);
    update_at(&engine, true, 0);

    /* A quick tap (not long-idle) is a plain attentive; no tap-streak. */
    const copet_behavior_event_t touch = event(
        COPET_BEHAVIOR_EVENT_TOUCH_SHORT, 0);
    copet_behavior_post(&engine, &touch, 2000);
    CHECK(update_at(&engine, true, 2000) == COPET_BEHAVIOR_ATTENTIVE);
    copet_behavior_post(&engine, &touch, 2500);
    CHECK(update_at(&engine, true, 2500) == COPET_BEHAVIOR_ATTENTIVE);
}

static bool is_gentle_pet(copet_behavior_id_t id)
{
    return id == COPET_BEHAVIOR_HAPPY || id == COPET_BEHAVIOR_CAT;
}

static bool is_loving_pet(copet_behavior_id_t id)
{
    return id == COPET_BEHAVIOR_KAWAII || id == COPET_BEHAVIOR_LOVELY;
}

static void test_petting_hold(void)
{
    copet_behavior_t engine;
    copet_behavior_init(&engine, 0, 12);
    update_at(&engine, true, 0);

    /* Released -> neutral. The longer it is held, the happier the tier. */
    CHECK(update_pet(&engine, 0, 100) == COPET_BEHAVIOR_NEUTRAL);
    CHECK(update_pet(&engine, 100, 1100) == COPET_BEHAVIOR_ATTENTIVE);
    CHECK(is_gentle_pet(update_pet(&engine, 600, 1600)));
    CHECK(is_loving_pet(update_pet(&engine, 2000, 3000)));
    CHECK(copet_behavior_get_view(&engine)->source ==
          COPET_BEHAVIOR_SOURCE_INPUT);

    /* Letting go settles back to neutral. */
    CHECK(update_pet(&engine, 0, 3100) == COPET_BEHAVIOR_NEUTRAL);
}

static void test_petting_variety(void)
{
    /* Repeated pets into the same tier must not show the same emotion twice
     * in a row, and must stay within the positive pool. */
    copet_behavior_t engine;
    copet_behavior_init(&engine, 0, 7);
    update_at(&engine, true, 0);

    copet_behavior_id_t previous = COPET_BEHAVIOR_NEUTRAL;
    uint32_t now = 100;
    for (int session = 0; session < 6; ++session) {
        const copet_behavior_id_t id = update_pet(&engine, 700, now);
        CHECK(is_gentle_pet(id));
        CHECK(id != previous);
        previous = id;
        update_pet(&engine, 0, now + 50); /* release */
        now += 300;
    }
}

static void test_petting_cancels_p3(void)
{
    copet_behavior_t engine;
    copet_behavior_init(&engine, 0, 13);
    update_at(&engine, true, 0);
    engine.next_p3_ms = 1;
    const copet_behavior_id_t p3 = update_at(&engine, true, 1);
    CHECK(p3 == COPET_BEHAVIOR_ZEN || p3 == COPET_BEHAVIOR_DICE_ROLL);

    /* Petting interrupts an idle P3 activity in the same update. */
    CHECK(is_gentle_pet(update_pet(&engine, 700, 2000)));
}

static void test_chill_on_break(void)
{
    copet_behavior_t engine;
    copet_behavior_init(&engine, 0, 10);
    update_at(&engine, false, 0);

    copet_behavior_event_t focus = event(
        COPET_BEHAVIOR_EVENT_FOCUS_CHANGED,
        COPET_BEHAVIOR_FOCUS_RUNNING_BREAK);
    copet_behavior_post(&engine, &focus, 100);
    CHECK(update_at(&engine, false, 100) == COPET_BEHAVIOR_CHILL);
    CHECK(copet_behavior_get_view(&engine)->priority ==
          COPET_BEHAVIOR_PRIORITY_P2);

    /* Running work still wins as focused; a paused break returns to neutral. */
    focus.value = COPET_BEHAVIOR_FOCUS_RUNNING_WORK;
    copet_behavior_post(&engine, &focus, 200);
    CHECK(update_at(&engine, false, 200) == COPET_BEHAVIOR_FOCUSED);
    focus.value = COPET_BEHAVIOR_FOCUS_PAUSED_BREAK;
    copet_behavior_post(&engine, &focus, 300);
    CHECK(update_at(&engine, false, 300) == COPET_BEHAVIOR_NEUTRAL);
}

static void test_nervous_wifi_escalation(void)
{
    copet_behavior_t engine;
    copet_behavior_init(&engine, 0, 11);
    update_at(&engine, true, 0);
    const copet_behavior_event_t wifi_on = event(
        COPET_BEHAVIOR_EVENT_WIFI_CHANGED, 1);
    copet_behavior_post(&engine, &wifi_on, 100);
    CHECK(update_at(&engine, true, 100) == COPET_BEHAVIOR_CONNECTING);

    /* Before the 6 s threshold it is still plain connecting. */
    CHECK(update_at(&engine, true, 5000) == COPET_BEHAVIOR_CONNECTING);

    /* At 6 s of connecting a single nervous glance appears. */
    CHECK(update_at(&engine, true, 6100) == COPET_BEHAVIOR_NERVOUS);
    CHECK(copet_behavior_get_view(&engine)->priority ==
          COPET_BEHAVIOR_PRIORITY_P1);

    /* After it ends, connecting resumes and nervous does not repeat. */
    CHECK(update_at(&engine, true, 7700) == COPET_BEHAVIOR_CONNECTING);
    CHECK(update_at(&engine, true, 9000) == COPET_BEHAVIOR_CONNECTING);

    /* Past the 10 s connecting window the face returns to neutral. */
    CHECK(update_at(&engine, true, 10100) == COPET_BEHAVIOR_NEUTRAL);

    /* A fresh connecting episode (off then on) can show nervous again. */
    const copet_behavior_event_t wifi_off = event(
        COPET_BEHAVIOR_EVENT_WIFI_CHANGED, 0);
    copet_behavior_post(&engine, &wifi_off, 15000);
    copet_behavior_post(&engine, &wifi_on, 20000);
    CHECK(update_at(&engine, true, 26100) == COPET_BEHAVIOR_NERVOUS);
}

static void test_wraparound(void)
{
    copet_behavior_t engine;
    const uint32_t start = UINT32_MAX - 100U;
    copet_behavior_init(&engine, start, 8);
    update_at(&engine, true, start);
    const copet_behavior_event_t boot = event(
        COPET_BEHAVIOR_EVENT_BOOT_COMPLETED, 0);
    copet_behavior_post(&engine, &boot, start);
    CHECK(update_at(&engine, true, UINT32_MAX - 1U) ==
          COPET_BEHAVIOR_ALERT);
    CHECK(update_at(&engine, true, 1498U) == COPET_BEHAVIOR_ALERT);
    CHECK(update_at(&engine, true, 1499U) == COPET_BEHAVIOR_NEUTRAL);
}

int main(void)
{
    CHECK(sizeof(copet_behavior_t) <= 512U);
    test_boot_lifecycle();
    test_priority_and_interruption();
    test_equal_priority_and_wifi_deadline();
    test_touch_followup();
    test_focus_source();
    test_hit_is_angry();
    test_p3_cooldown_history_and_cancel();
    test_tap_is_attentive();
    test_petting_hold();
    test_petting_variety();
    test_petting_cancels_p3();
    test_chill_on_break();
    test_nervous_wifi_escalation();
    test_wraparound();
    TEST_REPORT("copet_behavior");
}
