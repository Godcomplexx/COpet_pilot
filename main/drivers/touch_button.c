#include "drivers/touch_button.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

enum {
    TOUCH_PIN = 13,
    TOUCH_DEBOUNCE_MS = 25,
    TOUCH_LONG_PRESS_MS = 1000,
    TOUCH_POLL_MS = 5,          /* dedicated task polls fast so short taps stick */
    TOUCH_QUEUE_LEN = 8,
};

static const char *TAG = "touch_button";
static QueueHandle_t s_event_queue;
static volatile bool s_stable_pressed;

static void touch_task(void *argument)
{
    (void)argument;

    bool raw_pressed = gpio_get_level(TOUCH_PIN) != 0;
    bool stable_pressed = raw_pressed;
    bool long_reported = false;
    int64_t raw_changed_ms = esp_timer_get_time() / 1000;
    int64_t pressed_since_ms = raw_changed_ms;
    s_stable_pressed = stable_pressed;

    while (true) {
        const int64_t now_ms = esp_timer_get_time() / 1000;
        const bool sampled = gpio_get_level(TOUCH_PIN) != 0;
        if (sampled != raw_pressed) {
            raw_pressed = sampled;
            raw_changed_ms = now_ms;
        }

        if (raw_pressed != stable_pressed &&
            now_ms - raw_changed_ms >= TOUCH_DEBOUNCE_MS) {
            stable_pressed = raw_pressed;
            s_stable_pressed = stable_pressed;
            if (stable_pressed) {
                pressed_since_ms = now_ms;
                long_reported = false;
            } else if (!long_reported) {
                const touch_button_event_t event = TOUCH_BUTTON_EVENT_SHORT;
                (void)xQueueSend(s_event_queue, &event, 0);
            }
        }

        if (stable_pressed && !long_reported &&
            now_ms - pressed_since_ms >= TOUCH_LONG_PRESS_MS) {
            long_reported = true;
            const touch_button_event_t event = TOUCH_BUTTON_EVENT_LONG;
            (void)xQueueSend(s_event_queue, &event, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));
    }
}

esp_err_t touch_button_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << TOUCH_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const esp_err_t result = gpio_config(&config);
    if (result != ESP_OK) {
        return result;
    }

    s_event_queue =
        xQueueCreate(TOUCH_QUEUE_LEN, sizeof(touch_button_event_t));
    if (s_event_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const BaseType_t created =
        xTaskCreate(touch_task, "touch", 2048, NULL, 6, NULL);
    if (created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "TTP223 ready on GPIO%d, active HIGH (polled task)",
             TOUCH_PIN);
    return ESP_OK;
}

touch_button_event_t touch_button_poll(int64_t now_ms)
{
    (void)now_ms; /* the polling task owns timing now */
    touch_button_event_t event = TOUCH_BUTTON_EVENT_NONE;
    if (s_event_queue != NULL) {
        (void)xQueueReceive(s_event_queue, &event, 0);
    }
    return event;
}

bool touch_button_is_pressed(void)
{
    return s_stable_pressed;
}
