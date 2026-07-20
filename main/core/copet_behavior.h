#ifndef COPET_BEHAVIOR_H
#define COPET_BEHAVIOR_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    COPET_BEHAVIOR_PRIORITY_P0 = 0,
    COPET_BEHAVIOR_PRIORITY_P1 = 1,
    COPET_BEHAVIOR_PRIORITY_P2 = 2,
    COPET_BEHAVIOR_PRIORITY_P3 = 3,
} copet_behavior_priority_t;

typedef enum {
    COPET_BEHAVIOR_NEUTRAL,
    COPET_BEHAVIOR_ALERT,
    COPET_BEHAVIOR_ATTENTIVE,
    COPET_BEHAVIOR_FOCUSED,
    COPET_BEHAVIOR_ANGRY,
    COPET_BEHAVIOR_DISORIENTED,
    COPET_BEHAVIOR_DOUBLE_BLINK,
    COPET_BEHAVIOR_LOOK_LEFT,
    COPET_BEHAVIOR_LOOK_RIGHT,
    COPET_BEHAVIOR_ACKNOWLEDGE,
    COPET_BEHAVIOR_CONNECTING,
    COPET_BEHAVIOR_ZEN,
    COPET_BEHAVIOR_DICE_ROLL,
    /* v2 group: richer touch affection, calm break, connection anxiety. */
    COPET_BEHAVIOR_HAPPY,
    COPET_BEHAVIOR_KAWAII,
    COPET_BEHAVIOR_CHILL,
    COPET_BEHAVIOR_NERVOUS,
    COPET_BEHAVIOR_LEGACY_SCARED,
    COPET_BEHAVIOR_ID_COUNT,
} copet_behavior_id_t;

typedef enum {
    COPET_BEHAVIOR_SOURCE_NONE,
    COPET_BEHAVIOR_SOURCE_BOOT,
    COPET_BEHAVIOR_SOURCE_INPUT,
    COPET_BEHAVIOR_SOURCE_MOTION,
    COPET_BEHAVIOR_SOURCE_FOCUS,
    COPET_BEHAVIOR_SOURCE_WIFI,
    COPET_BEHAVIOR_SOURCE_SCHEDULER,
} copet_behavior_source_t;

typedef enum {
    COPET_BEHAVIOR_EVENT_BOOT_COMPLETED,
    COPET_BEHAVIOR_EVENT_TOUCH_SHORT,
    COPET_BEHAVIOR_EVENT_ENCODER_LEFT,
    COPET_BEHAVIOR_EVENT_ENCODER_RIGHT,
    COPET_BEHAVIOR_EVENT_CONFIRM,
    COPET_BEHAVIOR_EVENT_MOTION_TILT_LEFT,
    COPET_BEHAVIOR_EVENT_MOTION_TILT_RIGHT,
    COPET_BEHAVIOR_EVENT_MOTION_SHAKEN_STRONG,
    COPET_BEHAVIOR_EVENT_MOTION_FALLING,
    COPET_BEHAVIOR_EVENT_FOCUS_CHANGED,
    COPET_BEHAVIOR_EVENT_WIFI_CHANGED,
    COPET_BEHAVIOR_EVENT_USER_ACTIVITY,
} copet_behavior_event_type_t;

typedef enum {
    COPET_BEHAVIOR_FOCUS_OFF,
    COPET_BEHAVIOR_FOCUS_READY_WORK,
    COPET_BEHAVIOR_FOCUS_RUNNING_WORK,
    COPET_BEHAVIOR_FOCUS_PAUSED_WORK,
    COPET_BEHAVIOR_FOCUS_READY_BREAK,
    COPET_BEHAVIOR_FOCUS_RUNNING_BREAK,
    COPET_BEHAVIOR_FOCUS_PAUSED_BREAK,
} copet_behavior_focus_state_t;

typedef struct {
    copet_behavior_event_type_t type;
    int32_t value;
} copet_behavior_event_t;

typedef struct {
    bool desk_active;
} copet_behavior_context_t;

typedef struct {
    copet_behavior_id_t id;
    copet_behavior_priority_t priority;
    copet_behavior_source_t source;
    uint32_t elapsed_ms;
    uint32_t duration_ms;
} copet_behavior_view_t;

/*
 * Pure state: no ESP-IDF handles and no dynamic allocation. Fields are public
 * so the object can live on the app stack, but callers should only use the API.
 */
typedef struct {
    copet_behavior_view_t view;
    copet_behavior_id_t transient_id;
    copet_behavior_id_t followup_id;
    copet_behavior_priority_t transient_priority;
    copet_behavior_source_t transient_source;
    uint32_t transient_started_ms;
    uint32_t transient_deadline_ms;
    uint32_t followup_duration_ms;
    uint32_t last_activity_ms;
    uint32_t last_touch_ms;
    uint32_t next_p3_ms;
    uint32_t wifi_started_ms;
    uint32_t focus_started_ms;
    uint32_t last_shake_ms;
    uint32_t random_state;
    uint32_t p3_last_completed_ms[2];
    copet_behavior_id_t p3_history[4];
    copet_behavior_focus_state_t focus_state;
    uint8_t p3_history_count;
    uint8_t touch_streak;
    bool p3_seen[2];
    bool wifi_connecting;
    bool nervous_shown;
    bool shake_pending;
    bool desk_active;
    bool initialized;
} copet_behavior_t;

void copet_behavior_init(copet_behavior_t *engine,
                         uint32_t now_ms,
                         uint32_t random_seed);
void copet_behavior_post(copet_behavior_t *engine,
                         const copet_behavior_event_t *event,
                         uint32_t now_ms);
void copet_behavior_update(copet_behavior_t *engine,
                           const copet_behavior_context_t *context,
                           uint32_t now_ms);
const copet_behavior_view_t *copet_behavior_get_view(
    const copet_behavior_t *engine);
const char *copet_behavior_label(copet_behavior_id_t id);
const char *copet_behavior_source_label(copet_behavior_source_t source);

#endif
