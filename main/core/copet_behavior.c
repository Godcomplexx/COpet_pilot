#include "core/copet_behavior.h"

#include <stddef.h>

enum {
    ALERT_MS = 1600,
    ATTENTIVE_MS = 3000,
    ANGRY_MS = 1800,
    DISORIENTED_MS = 1800,
    DOUBLE_BLINK_MS = 480,
    LOOK_MS = 1200,
    ACKNOWLEDGE_MS = 450,
    HAPPY_MS = 1200,
    KAWAII_MS = 2500,
    NERVOUS_MS = 1500,
    LEGACY_SCARED_MS = 1600,
    WIFI_CONNECTING_MAX_MS = 10000,
    WIFI_NERVOUS_MS = 6000,
    TOUCH_STREAK_MS = 1500,
    LONG_IDLE_TOUCH_MS = 30000,
    SHAKE_WINDOW_MS = 5000,
    P3_IDLE_MS = 90000,
    P3_RETRY_MIN_MS = 60000,
    P3_RETRY_SPAN_MS = 120000,
    P3_COOLDOWN_MS = 10 * 60 * 1000,
    P3_MIN_SHOWN_MS = 1000,
};

static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static uint32_t random_next(copet_behavior_t *engine)
{
    uint32_t value = engine->random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    engine->random_state = value;
    return value;
}

static bool is_p3(copet_behavior_id_t id)
{
    return id == COPET_BEHAVIOR_ZEN || id == COPET_BEHAVIOR_DICE_ROLL;
}

static int p3_index(copet_behavior_id_t id)
{
    return id == COPET_BEHAVIOR_ZEN ? 0 :
           id == COPET_BEHAVIOR_DICE_ROLL ? 1 : -1;
}

static uint32_t duration_for(copet_behavior_id_t id)
{
    switch (id) {
    case COPET_BEHAVIOR_ALERT: return ALERT_MS;
    case COPET_BEHAVIOR_ATTENTIVE: return ATTENTIVE_MS;
    case COPET_BEHAVIOR_ANGRY: return ANGRY_MS;
    case COPET_BEHAVIOR_DISORIENTED: return DISORIENTED_MS;
    case COPET_BEHAVIOR_DOUBLE_BLINK: return DOUBLE_BLINK_MS;
    case COPET_BEHAVIOR_LOOK_LEFT:
    case COPET_BEHAVIOR_LOOK_RIGHT: return LOOK_MS;
    case COPET_BEHAVIOR_ACKNOWLEDGE: return ACKNOWLEDGE_MS;
    case COPET_BEHAVIOR_HAPPY: return HAPPY_MS;
    case COPET_BEHAVIOR_KAWAII: return KAWAII_MS;
    case COPET_BEHAVIOR_NERVOUS: return NERVOUS_MS;
    case COPET_BEHAVIOR_ZEN: return 12000;
    case COPET_BEHAVIOR_DICE_ROLL: return 8000;
    case COPET_BEHAVIOR_LEGACY_SCARED: return LEGACY_SCARED_MS;
    case COPET_BEHAVIOR_NEUTRAL:
    case COPET_BEHAVIOR_FOCUSED:
    case COPET_BEHAVIOR_CHILL:
    case COPET_BEHAVIOR_CONNECTING:
    case COPET_BEHAVIOR_ID_COUNT:
    default: return 0;
    }
}

static copet_behavior_priority_t priority_for(copet_behavior_id_t id)
{
    if (id == COPET_BEHAVIOR_LEGACY_SCARED) {
        return COPET_BEHAVIOR_PRIORITY_P0;
    }
    if (id == COPET_BEHAVIOR_FOCUSED || id == COPET_BEHAVIOR_NEUTRAL ||
        id == COPET_BEHAVIOR_CHILL) {
        return COPET_BEHAVIOR_PRIORITY_P2;
    }
    if (is_p3(id)) {
        return COPET_BEHAVIOR_PRIORITY_P3;
    }
    return COPET_BEHAVIOR_PRIORITY_P1;
}

static void schedule_next_p3(copet_behavior_t *engine, uint32_t now_ms)
{
    const uint32_t delay = P3_RETRY_MIN_MS +
        random_next(engine) % (P3_RETRY_SPAN_MS + 1U);
    engine->next_p3_ms = now_ms + delay;
}

static void record_p3(copet_behavior_t *engine,
                      copet_behavior_id_t id,
                      uint32_t now_ms)
{
    const int index = p3_index(id);
    if (index < 0) { return; }

    for (int item = 3; item > 0; --item) {
        engine->p3_history[item] = engine->p3_history[item - 1];
    }
    engine->p3_history[0] = id;
    if (engine->p3_history_count < 4U) {
        ++engine->p3_history_count;
    }
    engine->p3_last_completed_ms[index] = now_ms;
    engine->p3_seen[index] = true;
    schedule_next_p3(engine, now_ms);
}

static void finish_transient(copet_behavior_t *engine,
                             uint32_t now_ms,
                             bool allow_followup)
{
    const copet_behavior_id_t finished = engine->transient_id;
    const uint32_t shown_ms = now_ms - engine->transient_started_ms;
    if (is_p3(finished) && shown_ms >= P3_MIN_SHOWN_MS) {
        record_p3(engine, finished, now_ms);
    }

    const copet_behavior_id_t followup = engine->followup_id;
    const uint32_t followup_duration = engine->followup_duration_ms;
    engine->transient_id = COPET_BEHAVIOR_NEUTRAL;
    engine->followup_id = COPET_BEHAVIOR_NEUTRAL;
    engine->transient_deadline_ms = 0;
    engine->followup_duration_ms = 0;

    if (allow_followup && followup != COPET_BEHAVIOR_NEUTRAL &&
        engine->desk_active) {
        engine->transient_id = followup;
        engine->transient_priority = priority_for(followup);
        engine->transient_source = COPET_BEHAVIOR_SOURCE_INPUT;
        engine->transient_started_ms = now_ms;
        engine->transient_deadline_ms = now_ms + followup_duration;
    }
}

static bool start_transient(copet_behavior_t *engine,
                            copet_behavior_id_t id,
                            copet_behavior_source_t source,
                            uint32_t now_ms)
{
    const copet_behavior_priority_t priority = priority_for(id);
    if (engine->transient_id != COPET_BEHAVIOR_NEUTRAL) {
        if (engine->transient_priority < priority) {
            return false;
        }
        finish_transient(engine, now_ms, false);
    }

    engine->transient_id = id;
    engine->transient_priority = priority;
    engine->transient_source = source;
    engine->transient_started_ms = now_ms;
    engine->transient_deadline_ms = now_ms + duration_for(id);
    return true;
}

static void register_activity(copet_behavior_t *engine, uint32_t now_ms)
{
    if (is_p3(engine->transient_id)) {
        finish_transient(engine, now_ms, false);
    }
    engine->last_activity_ms = now_ms;
    engine->next_p3_ms = now_ms + P3_IDLE_MS;
}

static bool p3_in_history(const copet_behavior_t *engine,
                          copet_behavior_id_t id)
{
    for (uint8_t index = 0; index < engine->p3_history_count; ++index) {
        if (engine->p3_history[index] == id) { return true; }
    }
    return false;
}

static bool p3_cooled_down(const copet_behavior_t *engine,
                           copet_behavior_id_t id,
                           uint32_t now_ms)
{
    const int index = p3_index(id);
    return index >= 0 &&
        (!engine->p3_seen[index] ||
         time_reached(now_ms,
                      engine->p3_last_completed_ms[index] + P3_COOLDOWN_MS));
}

static copet_behavior_id_t choose_p3(copet_behavior_t *engine,
                                     uint32_t now_ms)
{
    static const copet_behavior_id_t candidates[] = {
        COPET_BEHAVIOR_ZEN,
        COPET_BEHAVIOR_DICE_ROLL,
    };
    copet_behavior_id_t preferred[2];
    uint8_t preferred_count = 0;
    copet_behavior_id_t cooled[2];
    uint8_t cooled_count = 0;
    const copet_behavior_id_t last = engine->p3_history_count > 0
        ? engine->p3_history[0] : COPET_BEHAVIOR_NEUTRAL;

    for (uint8_t index = 0; index < 2U; ++index) {
        const copet_behavior_id_t id = candidates[index];
        if (id == last || !p3_cooled_down(engine, id, now_ms)) {
            continue;
        }
        cooled[cooled_count++] = id;
        if (!p3_in_history(engine, id)) {
            preferred[preferred_count++] = id;
        }
    }

    if (preferred_count > 0U) {
        return preferred[random_next(engine) % preferred_count];
    }
    if (cooled_count == 0U) {
        return COPET_BEHAVIOR_NEUTRAL;
    }
    if (cooled_count == 1U) {
        return cooled[0];
    }

    const int first = p3_index(cooled[0]);
    const int second = p3_index(cooled[1]);
    const uint32_t first_age = now_ms - engine->p3_last_completed_ms[first];
    const uint32_t second_age = now_ms - engine->p3_last_completed_ms[second];
    return first_age >= second_age ? cooled[0] : cooled[1];
}

static void update_view(copet_behavior_t *engine, uint32_t now_ms)
{
    copet_behavior_id_t id = COPET_BEHAVIOR_NEUTRAL;
    copet_behavior_source_t source = COPET_BEHAVIOR_SOURCE_NONE;
    uint32_t started_ms = now_ms;
    uint32_t duration_ms = 0;

    if (engine->transient_id != COPET_BEHAVIOR_NEUTRAL) {
        id = engine->transient_id;
        source = engine->transient_source;
        started_ms = engine->transient_started_ms;
        duration_ms = duration_for(id);
    } else if (engine->wifi_connecting &&
               !time_reached(now_ms,
                             engine->wifi_started_ms +
                                 WIFI_CONNECTING_MAX_MS)) {
        id = COPET_BEHAVIOR_CONNECTING;
        source = COPET_BEHAVIOR_SOURCE_WIFI;
        started_ms = engine->wifi_started_ms;
        duration_ms = WIFI_CONNECTING_MAX_MS;
    } else if (engine->focus_state == COPET_BEHAVIOR_FOCUS_RUNNING_WORK) {
        id = COPET_BEHAVIOR_FOCUSED;
        source = COPET_BEHAVIOR_SOURCE_FOCUS;
        started_ms = engine->focus_started_ms;
    } else if (engine->focus_state == COPET_BEHAVIOR_FOCUS_RUNNING_BREAK) {
        id = COPET_BEHAVIOR_CHILL;
        source = COPET_BEHAVIOR_SOURCE_FOCUS;
        started_ms = engine->focus_started_ms;
    }

    engine->view.id = id;
    engine->view.priority =
        engine->transient_id != COPET_BEHAVIOR_NEUTRAL
            ? engine->transient_priority
            : priority_for(id);
    engine->view.source = source;
    engine->view.elapsed_ms = now_ms - started_ms;
    engine->view.duration_ms = duration_ms;
}

void copet_behavior_init(copet_behavior_t *engine,
                         uint32_t now_ms,
                         uint32_t random_seed)
{
    if (engine == NULL) { return; }
    *engine = (copet_behavior_t){0};
    engine->transient_id = COPET_BEHAVIOR_NEUTRAL;
    engine->followup_id = COPET_BEHAVIOR_NEUTRAL;
    engine->view.id = COPET_BEHAVIOR_NEUTRAL;
    engine->view.priority = COPET_BEHAVIOR_PRIORITY_P2;
    engine->last_activity_ms = now_ms;
    engine->next_p3_ms = now_ms + P3_IDLE_MS;
    engine->random_state = random_seed == 0U ? 0xC0BE7A11U : random_seed;
    engine->focus_state = COPET_BEHAVIOR_FOCUS_OFF;
    engine->initialized = true;
}

void copet_behavior_post(copet_behavior_t *engine,
                         const copet_behavior_event_t *event,
                         uint32_t now_ms)
{
    if (engine == NULL || event == NULL || !engine->initialized) { return; }

    switch (event->type) {
    case COPET_BEHAVIOR_EVENT_BOOT_COMPLETED:
        start_transient(engine, COPET_BEHAVIOR_ALERT,
                        COPET_BEHAVIOR_SOURCE_BOOT, now_ms);
        break;
    case COPET_BEHAVIOR_EVENT_TOUCH_SHORT: {
        const bool long_idle = now_ms - engine->last_activity_ms >=
                               LONG_IDLE_TOUCH_MS;
        /* A quick sequence of taps escalates attentive -> happy -> kawaii. */
        const bool in_streak = !long_idle &&
            (now_ms - engine->last_touch_ms) <= TOUCH_STREAK_MS;
        engine->touch_streak = in_streak
            ? (uint8_t)(engine->touch_streak + 1U) : 1U;
        engine->last_touch_ms = now_ms;
        register_activity(engine, now_ms);
        if (long_idle && start_transient(
                engine, COPET_BEHAVIOR_DOUBLE_BLINK,
                COPET_BEHAVIOR_SOURCE_INPUT, now_ms)) {
            engine->followup_id = COPET_BEHAVIOR_ATTENTIVE;
            engine->followup_duration_ms = ATTENTIVE_MS;
        } else if (engine->touch_streak >= 3U) {
            start_transient(engine, COPET_BEHAVIOR_KAWAII,
                            COPET_BEHAVIOR_SOURCE_INPUT, now_ms);
        } else if (engine->touch_streak == 2U) {
            start_transient(engine, COPET_BEHAVIOR_HAPPY,
                            COPET_BEHAVIOR_SOURCE_INPUT, now_ms);
        } else {
            start_transient(engine, COPET_BEHAVIOR_ATTENTIVE,
                            COPET_BEHAVIOR_SOURCE_INPUT, now_ms);
        }
        break;
    }
    case COPET_BEHAVIOR_EVENT_ENCODER_LEFT:
        register_activity(engine, now_ms);
        start_transient(engine, COPET_BEHAVIOR_LOOK_LEFT,
                        COPET_BEHAVIOR_SOURCE_INPUT, now_ms);
        break;
    case COPET_BEHAVIOR_EVENT_ENCODER_RIGHT:
        register_activity(engine, now_ms);
        start_transient(engine, COPET_BEHAVIOR_LOOK_RIGHT,
                        COPET_BEHAVIOR_SOURCE_INPUT, now_ms);
        break;
    case COPET_BEHAVIOR_EVENT_CONFIRM:
        register_activity(engine, now_ms);
        start_transient(engine, COPET_BEHAVIOR_ACKNOWLEDGE,
                        COPET_BEHAVIOR_SOURCE_INPUT, now_ms);
        break;
    case COPET_BEHAVIOR_EVENT_MOTION_TILT_LEFT:
    case COPET_BEHAVIOR_EVENT_MOTION_TILT_RIGHT:
        register_activity(engine, now_ms);
        start_transient(engine, COPET_BEHAVIOR_DISORIENTED,
                        COPET_BEHAVIOR_SOURCE_MOTION, now_ms);
        break;
    case COPET_BEHAVIOR_EVENT_MOTION_SHAKEN_STRONG:
        register_activity(engine, now_ms);
        if (engine->transient_id != COPET_BEHAVIOR_NEUTRAL &&
            engine->transient_priority == COPET_BEHAVIOR_PRIORITY_P0) {
            break;
        }
        if (engine->shake_pending &&
            !time_reached(now_ms, engine->last_shake_ms + SHAKE_WINDOW_MS)) {
            engine->shake_pending = false;
            start_transient(engine, COPET_BEHAVIOR_ANGRY,
                            COPET_BEHAVIOR_SOURCE_MOTION, now_ms);
        } else {
            engine->shake_pending = true;
            engine->last_shake_ms = now_ms;
            start_transient(engine, COPET_BEHAVIOR_LEGACY_SCARED,
                            COPET_BEHAVIOR_SOURCE_MOTION, now_ms);
            engine->transient_priority = COPET_BEHAVIOR_PRIORITY_P1;
        }
        break;
    case COPET_BEHAVIOR_EVENT_MOTION_FALLING:
        register_activity(engine, now_ms);
        engine->shake_pending = false;
        start_transient(engine, COPET_BEHAVIOR_LEGACY_SCARED,
                        COPET_BEHAVIOR_SOURCE_MOTION, now_ms);
        break;
    case COPET_BEHAVIOR_EVENT_FOCUS_CHANGED:
        if (event->value >= COPET_BEHAVIOR_FOCUS_OFF &&
            event->value <= COPET_BEHAVIOR_FOCUS_PAUSED_BREAK) {
            const copet_behavior_focus_state_t new_state =
                (copet_behavior_focus_state_t)event->value;
            if ((new_state == COPET_BEHAVIOR_FOCUS_RUNNING_WORK ||
                 new_state == COPET_BEHAVIOR_FOCUS_RUNNING_BREAK) &&
                engine->focus_state != new_state) {
                engine->focus_started_ms = now_ms;
            }
            engine->focus_state = new_state;
        }
        break;
    case COPET_BEHAVIOR_EVENT_WIFI_CHANGED: {
        const bool connecting = event->value != 0;
        if (connecting && !engine->wifi_connecting) {
            engine->wifi_started_ms = now_ms;
            engine->nervous_shown = false;
        }
        engine->wifi_connecting = connecting;
        break;
    }
    case COPET_BEHAVIOR_EVENT_USER_ACTIVITY:
        register_activity(engine, now_ms);
        break;
    }
}

void copet_behavior_update(copet_behavior_t *engine,
                           const copet_behavior_context_t *context,
                           uint32_t now_ms)
{
    if (engine == NULL || context == NULL || !engine->initialized) { return; }

    if (engine->desk_active != context->desk_active) {
        engine->desk_active = context->desk_active;
        register_activity(engine, now_ms);
    }
    if (engine->shake_pending &&
        time_reached(now_ms, engine->last_shake_ms + SHAKE_WINDOW_MS)) {
        engine->shake_pending = false;
    }
    if (engine->transient_id != COPET_BEHAVIOR_NEUTRAL &&
        time_reached(now_ms, engine->transient_deadline_ms)) {
        finish_transient(engine, now_ms, true);
    }

    /* A P1 nervous glance appears when Wi-Fi has been connecting too long,
     * once per connecting episode, only while nothing else is transient. */
    if (engine->wifi_connecting && !engine->nervous_shown &&
        engine->transient_id == COPET_BEHAVIOR_NEUTRAL &&
        time_reached(now_ms, engine->wifi_started_ms + WIFI_NERVOUS_MS) &&
        !time_reached(now_ms,
                      engine->wifi_started_ms + WIFI_CONNECTING_MAX_MS)) {
        if (start_transient(engine, COPET_BEHAVIOR_NERVOUS,
                            COPET_BEHAVIOR_SOURCE_WIFI, now_ms)) {
            engine->nervous_shown = true;
        }
    }

    const bool higher_active =
        engine->transient_id != COPET_BEHAVIOR_NEUTRAL ||
        (engine->wifi_connecting &&
         !time_reached(now_ms,
                       engine->wifi_started_ms + WIFI_CONNECTING_MAX_MS)) ||
        engine->focus_state == COPET_BEHAVIOR_FOCUS_RUNNING_WORK ||
        engine->focus_state == COPET_BEHAVIOR_FOCUS_RUNNING_BREAK;
    if (engine->desk_active && !higher_active &&
        time_reached(now_ms, engine->next_p3_ms)) {
        const copet_behavior_id_t id = choose_p3(engine, now_ms);
        if (id == COPET_BEHAVIOR_NEUTRAL) {
            engine->next_p3_ms = now_ms + P3_RETRY_MIN_MS;
        } else {
            start_transient(engine, id,
                            COPET_BEHAVIOR_SOURCE_SCHEDULER, now_ms);
        }
    }

    update_view(engine, now_ms);
}

const copet_behavior_view_t *copet_behavior_get_view(
    const copet_behavior_t *engine)
{
    return engine == NULL ? NULL : &engine->view;
}

const char *copet_behavior_label(copet_behavior_id_t id)
{
    switch (id) {
    case COPET_BEHAVIOR_ALERT: return "ALERT";
    case COPET_BEHAVIOR_ATTENTIVE: return "ATTENTIVE";
    case COPET_BEHAVIOR_FOCUSED: return "FOCUSED";
    case COPET_BEHAVIOR_ANGRY: return "ANGRY";
    case COPET_BEHAVIOR_DISORIENTED: return "DIZZY";
    case COPET_BEHAVIOR_DOUBLE_BLINK: return "DOUBLE BLINK";
    case COPET_BEHAVIOR_LOOK_LEFT: return "LOOK LEFT";
    case COPET_BEHAVIOR_LOOK_RIGHT: return "LOOK RIGHT";
    case COPET_BEHAVIOR_ACKNOWLEDGE: return "ACK";
    case COPET_BEHAVIOR_CONNECTING: return "CONNECTING";
    case COPET_BEHAVIOR_ZEN: return "ZEN";
    case COPET_BEHAVIOR_DICE_ROLL: return "DICE";
    case COPET_BEHAVIOR_HAPPY: return "HAPPY";
    case COPET_BEHAVIOR_KAWAII: return "KAWAII";
    case COPET_BEHAVIOR_CHILL: return "CHILL";
    case COPET_BEHAVIOR_NERVOUS: return "NERVOUS";
    case COPET_BEHAVIOR_LEGACY_SCARED: return "SCARED";
    case COPET_BEHAVIOR_NEUTRAL:
    case COPET_BEHAVIOR_ID_COUNT:
    default: return "NEUTRAL";
    }
}

const char *copet_behavior_source_label(copet_behavior_source_t source)
{
    switch (source) {
    case COPET_BEHAVIOR_SOURCE_BOOT: return "boot";
    case COPET_BEHAVIOR_SOURCE_INPUT: return "input";
    case COPET_BEHAVIOR_SOURCE_MOTION: return "motion";
    case COPET_BEHAVIOR_SOURCE_FOCUS: return "focus";
    case COPET_BEHAVIOR_SOURCE_WIFI: return "wifi";
    case COPET_BEHAVIOR_SOURCE_SCHEDULER: return "scheduler";
    case COPET_BEHAVIOR_SOURCE_NONE:
    default: return "none";
    }
}
