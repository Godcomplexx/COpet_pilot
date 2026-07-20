#include "drivers/touch_button.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

enum {
    TOUCH_PIN = 13,
    TOUCH_DEBOUNCE_MS = 40,
    TOUCH_LONG_PRESS_MS = 1000,
};

static const char *TAG = "touch_button";
static bool s_raw_pressed;
static bool s_stable_pressed;
static bool s_long_reported;
static int64_t s_raw_changed_ms;
static int64_t s_pressed_since_ms;

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

    const int64_t now_ms = esp_timer_get_time() / 1000;
    s_raw_pressed = gpio_get_level(TOUCH_PIN) != 0;
    s_stable_pressed = s_raw_pressed;
    s_raw_changed_ms = now_ms;
    s_pressed_since_ms = now_ms;
    s_long_reported = false;

    ESP_LOGI(TAG, "TTP223 ready on GPIO%d, active HIGH", TOUCH_PIN);
    return ESP_OK;
}

touch_button_event_t touch_button_poll(int64_t now_ms)
{
    const bool raw_pressed = gpio_get_level(TOUCH_PIN) != 0;
    if (raw_pressed != s_raw_pressed) {
        s_raw_pressed = raw_pressed;
        s_raw_changed_ms = now_ms;
    }

    if (s_raw_pressed != s_stable_pressed &&
        now_ms - s_raw_changed_ms >= TOUCH_DEBOUNCE_MS) {
        s_stable_pressed = s_raw_pressed;

        if (s_stable_pressed) {
            s_pressed_since_ms = now_ms;
            s_long_reported = false;
        } else if (!s_long_reported) {
            return TOUCH_BUTTON_EVENT_SHORT;
        }
    }

    if (s_stable_pressed && !s_long_reported &&
        now_ms - s_pressed_since_ms >= TOUCH_LONG_PRESS_MS) {
        s_long_reported = true;
        return TOUCH_BUTTON_EVENT_LONG;
    }

    return TOUCH_BUTTON_EVENT_NONE;
}

bool touch_button_is_pressed(void)
{
    return s_stable_pressed;
}
