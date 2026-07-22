#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "core/copet_behavior.h"
#include "core/copet_modes.h"
#include "core/music_detector.h"
#include "drivers/copet_audio.h"
#include "drivers/copet_ble.h"
#include "drivers/copet_display.h"
#include "drivers/copet_encoder.h"
#include "drivers/copet_i2c.h"
#include "drivers/copet_sht31.h"
#include "drivers/mpu6050.h"
#include "drivers/touch_button.h"
#include "modes/assistant_mode.h"
#include "modes/desk_mode.h"
#include "modes/focus_mode.h"
#include "modes/menu_mode.h"
#include "modes/settings_mode.h"
#include "services/assistant_service.h"
#include "services/weather_service.h"
#include "services/wifi_service.h"
#include "ui/assistant_ui.h"
#include "ui/boot_ui.h"
#include "ui/desk_ui.h"
#include "ui/diag_ui.h"
#include "ui/focus_ui.h"
#include "ui/menu_ui.h"
#include "ui/settings_ui.h"
#include "ui/ui_canvas.h"

/*
 * CoPet integration layer for the ESP32-WROOM-32 DevKit shown in the photos.
 *
 * app_main owns only the boot sequence and the main loop that wires inputs to
 * mode modules and mode state to renderers. The hardware lives behind driver
 * modules (drivers/copet_display, copet_i2c, copet_sht31, copet_encoder,
 * mpu6050, touch_button, copet_audio); mode logic in modes/; drawing in ui/.
 * See docs 01 and 03.
 */

static const char *TAG = "copet_test";

enum {
    DESK_FRAME_INTERVAL_US = 125000,
    MENU_TIMEOUT_US = 10000000,
    MOTION_IMPACT_LOCK_MS = 1900,
    BOOT_STAGE_HOLD_MS = 80,
    BOOT_FINAL_HOLD_MS = 250,
};

static void show_boot_progress(uint8_t progress_percent, const char *status,
                               uint32_t hold_ms)
{
    boot_ui_render(copet_display_framebuffer(), COPET_DISPLAY_WIDTH,
                   COPET_DISPLAY_HEIGHT, progress_percent, status);
    ESP_ERROR_CHECK(copet_display_refresh());
    if (hold_ms > 0U) {
        vTaskDelay(pdMS_TO_TICKS(hold_ms));
    }
}

static void play_audio_event(bool audio_available,
                             copet_audio_event_t event)
{
    if (!audio_available) {
        return;
    }
    const esp_err_t result = copet_audio_play_event(event);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Audio event %d skipped: %s", event,
                 esp_err_to_name(result));
    }
}

/* Word count, used to size the assistant's synthesized "speech". */
static uint32_t count_words(const char *text)
{
    uint32_t words = 0;
    bool in_word = false;
    for (const char *p = text; *p != '\0'; ++p) {
        if (*p == ' ') {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            ++words;
        }
    }
    return words;
}

static void post_behavior(copet_behavior_t *behavior,
                          copet_behavior_event_type_t type,
                          int32_t value,
                          uint32_t now_ms)
{
    const copet_behavior_event_t event = {
        .type = type,
        .value = value,
    };
    copet_behavior_post(behavior, &event, now_ms);
}

static copet_behavior_focus_state_t behavior_focus_state(
    copet_mode_t mode, const focus_mode_t *focus)
{
    if (mode != COPET_MODE_FOCUS || focus == NULL) {
        return COPET_BEHAVIOR_FOCUS_OFF;
    }
    if (focus->break_phase) {
        if (focus->state == FOCUS_TIMER_RUNNING) {
            return COPET_BEHAVIOR_FOCUS_RUNNING_BREAK;
        }
        return focus->state == FOCUS_TIMER_PAUSED
            ? COPET_BEHAVIOR_FOCUS_PAUSED_BREAK
            : COPET_BEHAVIOR_FOCUS_READY_BREAK;
    }
    if (focus->state == FOCUS_TIMER_RUNNING) {
        return COPET_BEHAVIOR_FOCUS_RUNNING_WORK;
    }
    return focus->state == FOCUS_TIMER_PAUSED
        ? COPET_BEHAVIOR_FOCUS_PAUSED_WORK
        : COPET_BEHAVIOR_FOCUS_READY_WORK;
}

static bool wifi_status_is_connecting(wifi_service_status_t status)
{
    return status == WIFI_SERVICE_STARTING ||
           status == WIFI_SERVICE_CONNECTING ||
           status == WIFI_SERVICE_RETRY_WAIT;
}

static void render_active_mode(copet_mode_t mode, const desk_mode_t *desk,
                               const menu_mode_t *menu,
                               const focus_mode_t *focus,
                               const settings_mode_t *settings,
                               const assistant_mode_t *assistant,
                               const copet_behavior_view_t *behavior,
                               const char *network_status,
                               const weather_service_snapshot_t *weather,
                               desk_ui_environment_t environment,
                               copet_ble_status_t ble_status,
                               const char *ble_message)
{
    uint8_t *fb = copet_display_framebuffer();
    const int width = COPET_DISPLAY_WIDTH;
    const int height = COPET_DISPLAY_HEIGHT;

    switch (mode) {
    case COPET_MODE_DESK:
        desk_ui_render(fb, width, height,
                       desk_mode_get_view(desk), behavior, network_status,
                       weather, environment);
        break;
    case COPET_MODE_SETTINGS:
        settings_ui_render(fb, width, height,
                           desk_mode_get_view(desk), settings,
                           network_status, weather);
        break;
    case COPET_MODE_FOCUS:
        focus_ui_render(fb, width, height, focus);
        break;
    case COPET_MODE_ASSISTANT:
        assistant_ui_render(fb, width, height, assistant);
        break;
    case COPET_MODE_PHONE_BRIDGE:
        diag_ui_render_phone_bridge(fb, width, height,
                                    ble_status, ble_message);
        break;
    case COPET_MODE_MENU:
    default:
        menu_ui_render(fb, width, height, menu, network_status);
        break;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "CoPet Desk + menu + hardware test");
    ESP_LOGI(TAG, "Board target: ESP32-WROOM-32");

    ESP_ERROR_CHECK(copet_display_init());
    show_boot_progress(10, "DISPLAY READY", BOOT_STAGE_HOLD_MS);
    ESP_ERROR_CHECK(copet_i2c_init());
    ESP_ERROR_CHECK(copet_sht31_init(copet_i2c_bus()));
    ESP_ERROR_CHECK(touch_button_init());
    show_boot_progress(35, "INPUT READY", BOOT_STAGE_HOLD_MS);
    uint8_t mpu6050_address = 0;
    const esp_err_t mpu6050_init_result =
        mpu6050_init(copet_i2c_bus(), &mpu6050_address);
    const bool mpu6050_available = mpu6050_init_result == ESP_OK;
    if (mpu6050_available) {
        ESP_LOGI(TAG, "MPU6050 motion reactions enabled at 0x%02X",
                 mpu6050_address);
    } else {
        ESP_LOGW(TAG, "MPU6050 not found; motion reactions are waiting for hardware");
    }
    show_boot_progress(55, "SENSORS CHECKED", BOOT_STAGE_HOLD_MS);
    const esp_err_t wifi_init_result = wifi_service_start();
    if (wifi_init_result != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi unavailable: %s",
                 esp_err_to_name(wifi_init_result));
    }
    const esp_err_t weather_init_result = weather_service_start();
    if (weather_init_result != ESP_OK) {
        ESP_LOGE(TAG, "Outdoor weather unavailable: %s",
                 esp_err_to_name(weather_init_result));
    }
#if CONFIG_COPET_ASSISTANT_ENABLED
    const esp_err_t assistant_init_result = assistant_service_start();
    if (assistant_init_result != ESP_OK) {
        ESP_LOGE(TAG, "Assistant service unavailable: %s",
                 esp_err_to_name(assistant_init_result));
    }
#endif
    show_boot_progress(75, "NETWORK STARTED", BOOT_STAGE_HOLD_MS);
#if CONFIG_COPET_DIAGNOSTIC_BLE_ENABLED
    const esp_err_t ble_init_result = copet_ble_init();
    const bool ble_available = ble_init_result == ESP_OK;
    if (!ble_available) {
        ESP_LOGE(TAG, "Diagnostic BLE unavailable: %s",
                 esp_err_to_name(ble_init_result));
    }
#else
    const bool ble_available = false;
    ESP_LOGI(TAG, "Diagnostic BLE disabled; RAM reserved for network services");
#endif
    const esp_err_t audio_init_result = copet_audio_init();
    const bool audio_available = audio_init_result == ESP_OK;
    if (!audio_available) {
        ESP_LOGE(TAG, "Product audio unavailable: %s",
                 esp_err_to_name(audio_init_result));
    }
    show_boot_progress(90, "SERVICES CHECKED", BOOT_STAGE_HOLD_MS);

    ESP_ERROR_CHECK(copet_encoder_start());

    bool sensor_ok = false;
    float temperature = 0.0f;
    float humidity = 0.0f;
    uint8_t sht31_address = 0;
    int64_t next_sensor_read_us = 0;
    int32_t displayed_encoder_position = INT32_MIN;
    uint8_t displayed_encoder_ab = UINT8_MAX;
    copet_mode_t mode = COPET_MODE_DESK;
    bool force_redraw = true;
    bool pet_active = false;
    int64_t pet_start_ms = 0;
    music_detector_t music;
    music_detector_init(&music);
#if CONFIG_COPET_MUSIC_DEBUG
    uint32_t last_music_debug_ms = 0;
#endif
    desk_mode_t desk;
    menu_mode_t menu;
    focus_mode_t focus;
    settings_mode_t settings;
    assistant_mode_t assistant;
    uint32_t displayed_assistant_revision = UINT32_MAX;
    copet_behavior_t behavior;
    copet_ble_status_t displayed_ble_status = COPET_BLE_OFF;
    char ble_message[65] = "NO MESSAGE";
    int64_t next_desk_frame_us = 0;
    int64_t menu_last_activity_us = 0;
    int64_t next_mpu6050_read_us = 0;
    desk_motion_event_t displayed_motion_event = DESK_MOTION_NONE;
    uint32_t motion_impact_until_ms = 0;
    int displayed_expression = -1;
    int displayed_vibe = -1;
    int displayed_behavior = -1;
    copet_behavior_focus_state_t published_focus_state =
        COPET_BEHAVIOR_FOCUS_OFF;
    wifi_service_status_t displayed_wifi_status = WIFI_SERVICE_OFF;
    weather_service_snapshot_t weather = {0};
    uint32_t displayed_weather_revision = UINT32_MAX;
    desk_ui_environment_t desk_environment = DESK_UI_ENVIRONMENT_ROOM;

    const uint32_t app_started_ms =
        (uint32_t)(esp_timer_get_time() / 1000);
    desk_mode_init(&desk, app_started_ms);
    copet_behavior_init(&behavior, app_started_ms, app_started_ms);
    menu_mode_init(&menu);
    focus_mode_init(&focus);
    settings_mode_init(&settings, true);
    assistant_mode_init(&assistant);
    if (audio_available) {
        copet_audio_set_enabled(settings.sound_enabled);
    }

    show_boot_progress(100, "DESK MODE", BOOT_FINAL_HOLD_MS);
    post_behavior(&behavior, COPET_BEHAVIOR_EVENT_BOOT_COMPLETED, 0,
                  (uint32_t)(esp_timer_get_time() / 1000));
    ESP_LOGI(TAG, "Mode: DESK");

    while (true) {
        const int64_t now_us = esp_timer_get_time();
        const int64_t now_ms = now_us / 1000;
        bool redraw = false;

        const touch_button_event_t touch_event =
            touch_button_poll(now_ms);
        if (touch_event == TOUCH_BUTTON_EVENT_SHORT) {
            ESP_LOGI(TAG, "TOUCH_SHORT");
            desk_mode_on_activity(&desk, (uint32_t)now_ms);
            if (mode == COPET_MODE_DESK) {
                post_behavior(&behavior, COPET_BEHAVIOR_EVENT_TOUCH_SHORT,
                              0, (uint32_t)now_ms);
            } else if (mode == COPET_MODE_MENU ||
                       mode == COPET_MODE_FOCUS ||
                       mode == COPET_MODE_SETTINGS ||
                       mode == COPET_MODE_ASSISTANT) {
                post_behavior(&behavior, COPET_BEHAVIOR_EVENT_CONFIRM,
                              0, (uint32_t)now_ms);
            } else {
                post_behavior(&behavior,
                              COPET_BEHAVIOR_EVENT_USER_ACTIVITY,
                              0, (uint32_t)now_ms);
            }
            if (mode == COPET_MODE_DESK) {
                desk_environment =
                    desk_environment == DESK_UI_ENVIRONMENT_ROOM
                        ? DESK_UI_ENVIRONMENT_OUTDOOR
                        : DESK_UI_ENVIRONMENT_ROOM;
                desk_mode_on_touch(&desk, (uint32_t)now_ms);
                play_audio_event(audio_available,
                                 COPET_AUDIO_VIEW_CHANGE);
                ESP_LOGI(TAG, "Desk touch reaction: %s; climate view: %s",
                         desk_mode_touch_reaction_label(
                             desk_mode_get_view(&desk)->touch_reaction),
                         desk_environment == DESK_UI_ENVIRONMENT_ROOM
                             ? "ROOM"
                             : "OUT");
                redraw = true;
            } else if (mode == COPET_MODE_MENU) {
                const menu_item_t *item = menu_mode_selected(&menu);
                play_audio_event(audio_available,
                                 COPET_AUDIO_MENU_CONFIRM);
                mode = item->mode;
                ESP_LOGI(TAG, "Mode opened: %s", item->label);
                if (mode == COPET_MODE_ASSISTANT) {
                    assistant_mode_init(&assistant); /* fresh IDLE on entry */
                }
                if (mode == COPET_MODE_PHONE_BRIDGE && ble_available) {
                    const esp_err_t result = copet_ble_start();
                    if (result != ESP_OK) {
                        ESP_LOGE(TAG, "BLE advertising failed: %s",
                                 esp_err_to_name(result));
                    }
                }
            } else if (mode == COPET_MODE_FOCUS) {
                const focus_toggle_result_t toggle_result =
                    focus_mode_toggle(&focus, now_us);
                if (toggle_result == FOCUS_TOGGLE_STARTED ||
                    toggle_result == FOCUS_TOGGLE_RESUMED) {
                    play_audio_event(audio_available,
                                     COPET_AUDIO_FOCUS_START);
                } else {
                    play_audio_event(audio_available,
                                     COPET_AUDIO_FOCUS_PAUSE);
                }
                if (toggle_result == FOCUS_TOGGLE_PAUSED) {
                    ESP_LOGI(TAG, "Focus timer paused at %lu seconds",
                             (unsigned long)focus.remaining_seconds);
                } else {
                    ESP_LOGI(TAG, "Focus timer %s: %s",
                             toggle_result == FOCUS_TOGGLE_RESUMED
                                 ? "resumed"
                                 : "started",
                             focus.break_phase ? "BREAK" : "WORK");
                }
            } else if (mode == COPET_MODE_SETTINGS) {
                const bool enabled = settings_mode_toggle_sound(&settings);
                if (audio_available) {
                    copet_audio_set_enabled(enabled);
                }
                ESP_LOGI(TAG, "Sound setting: %s",
                         settings_mode_sound_label(&settings));
            } else if (mode == COPET_MODE_ASSISTANT) {
                if (assistant.state == ASSISTANT_IDLE) {
                    const assistant_preset_t *preset =
                        assistant_mode_submit(&assistant, (uint32_t)now_ms);
                    if (preset != NULL) {
                        play_audio_event(audio_available,
                                         COPET_AUDIO_MENU_CONFIRM);
                        const esp_err_t sent = assistant_service_submit(
                            preset->type, preset->text);
                        if (sent != ESP_OK) {
                            assistant_mode_on_error(&assistant, "SERVICE BUSY",
                                                    (uint32_t)now_ms);
                        }
                        ESP_LOGI(TAG, "Assistant query: %s", preset->label);
                    }
                } else {
                    /* Tap dismisses a result/error (or cancels the wait). */
                    assistant_mode_back(&assistant);
                }
            }
            force_redraw = true;
        } else if (touch_event == TOUCH_BUTTON_EVENT_LONG) {
            ESP_LOGI(TAG, "TOUCH_LONG");
            desk_mode_on_activity(&desk, (uint32_t)now_ms);
            post_behavior(&behavior, COPET_BEHAVIOR_EVENT_USER_ACTIVITY,
                          0, (uint32_t)now_ms);
            if (mode != COPET_MODE_DESK) {
                if (mode == COPET_MODE_PHONE_BRIDGE && ble_available) {
                    copet_ble_stop();
                }
                if (mode == COPET_MODE_FOCUS) {
                    focus_mode_pause(&focus);
                }
                mode = COPET_MODE_DESK;
                next_desk_frame_us = 0;
                ESP_LOGI(TAG, "Mode: DESK");
            }
            force_redraw = true;
        }

        if (now_us >= next_sensor_read_us) {
            const esp_err_t result =
                copet_sht31_read(&temperature, &humidity, &sht31_address);
            sensor_ok = result == ESP_OK;
            if (sensor_ok) {
                ESP_LOGI(TAG,
                         "SHT3x 0x%02X: temperature=%.1f C, humidity=%.1f %%",
                         sht31_address, (double)temperature, (double)humidity);
            } else {
                ESP_LOGW(TAG,
                         "SHT3x not responding at 0x44/0x45: %s",
                         esp_err_to_name(result));
                sht31_address = 0;
            }
            desk_mode_set_environment(&desk, sensor_ok,
                                      temperature, humidity);
            next_sensor_read_us = now_us + 2000000;
            redraw = true;
        }

        if (mpu6050_available && now_us >= next_mpu6050_read_us) {
            mpu6050_sample_t sample;
            const esp_err_t motion_result = mpu6050_read(&sample);
            if (motion_result == ESP_OK) {
                const desk_motion_event_t motion_event =
                    desk_mode_set_motion_sample(
                        &desk, true,
                        sample.accel_x_g, sample.accel_y_g, sample.accel_z_g,
                        sample.gyro_x_dps, sample.gyro_y_dps,
                        sample.gyro_z_dps, (uint32_t)now_ms);
                if (motion_event != displayed_motion_event) {
                    ESP_LOGI(TAG,
                             "Motion reaction: %s; "
                             "accel=(%.2f, %.2f, %.2f) g; "
                             "gyro=(%.1f, %.1f, %.1f) dps",
                             desk_mode_motion_label(motion_event),
                             (double)sample.accel_x_g,
                             (double)sample.accel_y_g,
                             (double)sample.accel_z_g,
                             (double)sample.gyro_x_dps,
                             (double)sample.gyro_y_dps,
                             (double)sample.gyro_z_dps);
                    displayed_motion_event = motion_event;
                    if (motion_event == DESK_MOTION_FALLING) {
                        post_behavior(
                            &behavior,
                            COPET_BEHAVIOR_EVENT_MOTION_FALLING,
                            0, (uint32_t)now_ms);
                    } else if (motion_event == DESK_MOTION_SHAKEN) {
                        /* A hit -> angry; keep dizzy off for a moment so the
                         * angry reaction is not cut short by the follow-up
                         * tilt of being handled. */
                        post_behavior(
                            &behavior,
                            COPET_BEHAVIOR_EVENT_MOTION_SHAKEN_STRONG,
                            0, (uint32_t)now_ms);
                        play_audio_event(audio_available, COPET_AUDIO_ANGRY);
                        motion_impact_until_ms =
                            (uint32_t)now_ms + MOTION_IMPACT_LOCK_MS;
                    } else if ((motion_event == DESK_MOTION_TILTED ||
                                motion_event == DESK_MOTION_MOVED) &&
                               (uint32_t)now_ms >= motion_impact_until_ms) {
                        /* Carried / tilted / rotated -> dizzy (disoriented). */
                        post_behavior(
                            &behavior,
                            sample.accel_x_g < 0.0f
                                ? COPET_BEHAVIOR_EVENT_MOTION_TILT_LEFT
                                : COPET_BEHAVIOR_EVENT_MOTION_TILT_RIGHT,
                            0, (uint32_t)now_ms);
                    }
                }
                if (mode == COPET_MODE_DESK) { redraw = true; }
            } else {
                desk_mode_set_motion_sample(&desk, false,
                                            0, 0, 0, 0, 0, 0,
                                            (uint32_t)now_ms);
            }
            next_mpu6050_read_us = now_us + 100000;
        }

        if (mode == COPET_MODE_FOCUS) {
            const bool was_break = focus.break_phase;
            const focus_timer_state_t previous_state = focus.state;
            if (focus_mode_tick(&focus, now_us)) {
                if (previous_state == FOCUS_TIMER_RUNNING &&
                    focus.state == FOCUS_TIMER_READY) {
                    if (focus.break_phase && !was_break) {
                        ESP_LOGI(TAG,
                                 "Work complete; break ready, sessions=%lu",
                                 (unsigned long)focus.sessions);
                    } else if (!focus.break_phase && was_break) {
                        ESP_LOGI(TAG, "Break complete; work ready");
                    }
                    play_audio_event(audio_available,
                                     COPET_AUDIO_FOCUS_COMPLETE);
                }
                redraw = true;
            }
        }

#if CONFIG_COPET_ASSISTANT_ENABLED
        /* Client-side timeout, then fold any new service answer into the mode. */
        if (assistant_mode_tick(&assistant, (uint32_t)now_ms) &&
            mode == COPET_MODE_ASSISTANT) {
            redraw = true;
        }
        assistant_service_snapshot_t assistant_snapshot;
        assistant_service_get_snapshot(&assistant_snapshot);
        if (assistant_snapshot.revision != displayed_assistant_revision) {
            displayed_assistant_revision = assistant_snapshot.revision;
            if (assistant.state == ASSISTANT_WAITING) {
                if (assistant_snapshot.status == ASSISTANT_SERVICE_READY &&
                    assistant_snapshot.has_answer) {
                    assistant_mode_on_answer(&assistant,
                                             assistant_snapshot.text,
                                             assistant_snapshot.mood,
                                             (uint32_t)now_ms);
                    if (audio_available) {
                        /* Local skills speak real values from the vocabulary;
                         * anything else gets the retro robot babble. */
                        const assistant_preset_t *ap =
                            assistant_mode_selected_preset(&assistant);
                        speech_word_t words[SPEECH_MAX_WORDS];
                        int spoken = 0;
                        if (ap != NULL && strcmp(ap->type, "weather") == 0 &&
                            weather.has_data) {
                            const int temp = (int)(weather.temperature_c +
                                (weather.temperature_c >= 0.0f ? 0.5f : -0.5f));
                            spoken = copet_speech_weather(
                                temp, weather.weather_code, words,
                                SPEECH_MAX_WORDS);
                        } else if (ap != NULL &&
                                   strcmp(ap->type, "time") == 0) {
                            const time_t t_now = time(NULL);
                            struct tm local_time;
                            localtime_r(&t_now, &local_time);
                            if (local_time.tm_year + 1900 >= 2021) {
                                spoken = copet_speech_time(
                                    local_time.tm_hour, local_time.tm_min,
                                    words, SPEECH_MAX_WORDS);
                            }
                        }
                        if (spoken > 0) {
                            copet_audio_say(words, spoken);
                        } else {
                            copet_audio_speak(
                                count_words(assistant_snapshot.text));
                        }
                    }
                } else if (assistant_snapshot.status ==
                           ASSISTANT_SERVICE_ERROR) {
                    assistant_mode_on_error(&assistant,
                                            assistant_snapshot.text[0] != '\0'
                                                ? assistant_snapshot.text
                                                : "NO RESPONSE",
                                            (uint32_t)now_ms);
                }
            }
            if (mode == COPET_MODE_ASSISTANT) { redraw = true; }
        }
#endif

        const copet_ble_status_t ble_status =
            ble_available ? copet_ble_get_status() : COPET_BLE_ERROR;
        if (ble_status != displayed_ble_status) {
            displayed_ble_status = ble_status;
            if (mode == COPET_MODE_PHONE_BRIDGE) {
                redraw = true;
            }
        }
        if (copet_ble_take_message(ble_message, sizeof(ble_message))) {
            ESP_LOGI(TAG, "Phone message displayed: %s", ble_message);
            if (mode == COPET_MODE_PHONE_BRIDGE) {
                redraw = true;
            }
        }

        desk_mode_update(&desk, (uint32_t)now_ms);
        const desk_mode_view_t *desk_view = desk_mode_get_view(&desk);
        if ((int)desk_view->expression != displayed_expression ||
            (int)desk_view->vibe != displayed_vibe) {
            displayed_expression = desk_view->expression;
            displayed_vibe = desk_view->vibe;
            ESP_LOGI(TAG, "Desk emotion: %s, effect: %s, idle=%lu s",
                     desk_mode_expression_label(desk_view->expression),
                     desk_mode_vibe_label(desk_view->vibe),
                     (unsigned long)desk_view->inactivity_seconds);
        }
        /* Only Desk shows the live animated face; other screens redraw on
         * their own events (Focus timer tick, Wi-Fi/weather status, input). */
        if (mode == COPET_MODE_DESK && now_us >= next_desk_frame_us) {
            next_desk_frame_us = now_us + DESK_FRAME_INTERVAL_US;
            redraw = true;
        }

        const wifi_service_status_t wifi_status =
            wifi_service_get_status();
        if (wifi_status != displayed_wifi_status) {
            displayed_wifi_status = wifi_status;
            post_behavior(&behavior, COPET_BEHAVIOR_EVENT_WIFI_CHANGED,
                          wifi_status_is_connecting(wifi_status) ? 1 : 0,
                          (uint32_t)now_ms);
            ESP_LOGI(TAG, "Wi-Fi UI status: %s",
                     wifi_service_status_label(wifi_status));
            if (mode == COPET_MODE_DESK || mode == COPET_MODE_MENU ||
                mode == COPET_MODE_SETTINGS) {
                redraw = true;
            }
        }

        weather_service_get_snapshot(&weather);
        if (weather.revision != displayed_weather_revision) {
            displayed_weather_revision = weather.revision;
            ESP_LOGI(TAG, "Weather UI status: %s",
                     weather_service_status_label(weather.status));
            if (mode == COPET_MODE_DESK || mode == COPET_MODE_SETTINGS) {
                redraw = true;
            }
        }

        const int32_t encoder_position = copet_encoder_position();
        const uint8_t encoder_ab = copet_encoder_ab();
        if (encoder_position != displayed_encoder_position ||
            encoder_ab != displayed_encoder_ab) {
            const bool real_step =
                encoder_position != displayed_encoder_position &&
                displayed_encoder_position != INT32_MIN;
            if (real_step) {
                desk_mode_on_activity(&desk, (uint32_t)now_ms);
                const int32_t behavior_steps =
                    encoder_position - displayed_encoder_position;
                post_behavior(
                    &behavior,
                    behavior_steps < 0
                        ? COPET_BEHAVIOR_EVENT_ENCODER_LEFT
                        : COPET_BEHAVIOR_EVENT_ENCODER_RIGHT,
                    behavior_steps, (uint32_t)now_ms);
            }
            if (real_step && mode == COPET_MODE_DESK) {
                const int32_t logical_steps =
                    encoder_position - displayed_encoder_position;
                menu_mode_scroll(&menu, logical_steps);
                mode = COPET_MODE_MENU;
                menu_last_activity_us = now_us;
                play_audio_event(audio_available, COPET_AUDIO_MENU_MOVE);
                ESP_LOGI(TAG, "Mode: MENU, selected: %s",
                         menu_mode_selected(&menu)->label);
                force_redraw = true;
            } else if (real_step && mode == COPET_MODE_MENU) {
                const int32_t logical_steps =
                    encoder_position - displayed_encoder_position;
                menu_mode_scroll(&menu, logical_steps);
                play_audio_event(audio_available, COPET_AUDIO_MENU_MOVE);
                ESP_LOGI(TAG, "Menu selected: %s",
                         menu_mode_selected(&menu)->label);
                menu_last_activity_us = now_us;
                force_redraw = true;
            } else if (real_step && mode == COPET_MODE_FOCUS &&
                       focus.state == FOCUS_TIMER_READY) {
                const int32_t logical_steps =
                    encoder_position - displayed_encoder_position;
                focus_mode_select_preset(&focus, logical_steps);
                play_audio_event(audio_available, COPET_AUDIO_MENU_MOVE);
                ESP_LOGI(TAG, "Focus preset: %s",
                         focus_mode_preset_label(&focus));
                force_redraw = true;
            } else if (real_step && mode == COPET_MODE_ASSISTANT &&
                       assistant.state == ASSISTANT_IDLE) {
                const int32_t logical_steps =
                    encoder_position - displayed_encoder_position;
                assistant_mode_scroll(&assistant, logical_steps);
                play_audio_event(audio_available, COPET_AUDIO_MENU_MOVE);
                ESP_LOGI(TAG, "Assistant preset: %s",
                         assistant_mode_selected_preset(&assistant)->label);
                force_redraw = true;
            }
            displayed_encoder_position = encoder_position;
            displayed_encoder_ab = encoder_ab;
        }

        if (mode == COPET_MODE_MENU && menu_last_activity_us > 0 &&
            now_us - menu_last_activity_us >= MENU_TIMEOUT_US) {
            mode = COPET_MODE_DESK;
            next_desk_frame_us = 0;
            ESP_LOGI(TAG, "Menu timeout; Mode: DESK");
            force_redraw = true;
        }

        const copet_behavior_focus_state_t current_focus_state =
            behavior_focus_state(mode, &focus);
        if (current_focus_state != published_focus_state) {
            published_focus_state = current_focus_state;
            post_behavior(&behavior, COPET_BEHAVIOR_EVENT_FOCUS_CHANGED,
                          current_focus_state, (uint32_t)now_ms);
        }
        /* Petting: while the pad is held on Desk, measure how long, so the
         * behavior engine can escalate attentive -> happy -> kawaii. */
        uint32_t touch_hold_ms = 0;
        if (mode == COPET_MODE_DESK && touch_button_is_pressed()) {
            if (!pet_active) {
                pet_active = true;
                pet_start_ms = now_ms;
            }
            touch_hold_ms = (uint32_t)(now_ms - pet_start_ms);
            desk_mode_on_activity(&desk, (uint32_t)now_ms);
        } else {
            pet_active = false;
        }
        const uint8_t mic_level = copet_audio_get_mic_level();
        const uint8_t mic_zcr = copet_audio_get_mic_zcr();
        const bool music_present = music_detector_update(
            &music, mic_level, mic_zcr, (uint32_t)now_ms);
#if CONFIG_COPET_MUSIC_DEBUG
        if ((uint32_t)(now_ms - last_music_debug_ms) >= 1000U) {
            last_music_debug_ms = (uint32_t)now_ms;
            ESP_LOGI(TAG,
                     "music: level=%u beat=%u bpm=%u listening=%d",
                     (unsigned)mic_level,
                     (unsigned)music_detector_score(&music),
                     (unsigned)music.bpm,
                     (int)music_present);
        }
#endif
        const copet_behavior_context_t behavior_context = {
            .desk_active = mode == COPET_MODE_DESK,
            .touch_hold_ms = touch_hold_ms,
            .music_present = music_present,
        };
        copet_behavior_update(&behavior, &behavior_context,
                              (uint32_t)now_ms);
        const copet_behavior_view_t *behavior_view =
            copet_behavior_get_view(&behavior);
        if ((int)behavior_view->id != displayed_behavior) {
            const char *old_label = displayed_behavior < 0
                ? "START"
                : copet_behavior_label(
                      (copet_behavior_id_t)displayed_behavior);
            ESP_LOGI(TAG, "behavior: %s -> %s source=%s priority=P%u",
                     old_label, copet_behavior_label(behavior_view->id),
                     copet_behavior_source_label(behavior_view->source),
                     (unsigned)behavior_view->priority);
            displayed_behavior = behavior_view->id;
            redraw = true;
        }

        if (redraw || force_redraw) {
            render_active_mode(mode, &desk, &menu, &focus, &settings,
                               &assistant, behavior_view,
                               wifi_service_status_label(wifi_status),
                               &weather, desk_environment, ble_status,
                               ble_message);
            ESP_ERROR_CHECK(copet_display_refresh());
            force_redraw = false;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
