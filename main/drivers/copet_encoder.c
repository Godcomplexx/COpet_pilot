#include "drivers/copet_encoder.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum {
    ENCODER_PIN_A = 32,
    ENCODER_PIN_B = 33,
    ENCODER_DEBOUNCE_US = 12000,
};

static const char *TAG = "copet_encoder";

static volatile int32_t s_encoder_position;
static volatile uint8_t s_encoder_ab;

static void encoder_task(void *argument)
{
    (void)argument;

    /*
     * The tested three-contact mouse wheel has a three-state cycle:
     * 10 -> 11 -> 01 -> 10. The reverse direction uses the opposite order.
     * State 00 is not a detent and is ignored.
     */
    static const int8_t transition_table[16] = {
        0, 0, 0, 0,
        0, 0, 1, -1,
        0, -1, 0, 1,
        0, 1, -1, 0,
    };

    gpio_config_t config = {
        .pin_bit_mask =
            (1ULL << ENCODER_PIN_A) | (1ULL << ENCODER_PIN_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));

    uint8_t raw_state =
        ((uint8_t)gpio_get_level(ENCODER_PIN_A) << 1) |
        (uint8_t)gpio_get_level(ENCODER_PIN_B);
    uint8_t stable_state = raw_state;
    int64_t raw_changed_us = esp_timer_get_time();
    s_encoder_ab = stable_state;

    while (true) {
        const int64_t now_us = esp_timer_get_time();
        const uint8_t sampled_state =
            ((uint8_t)gpio_get_level(ENCODER_PIN_A) << 1) |
            (uint8_t)gpio_get_level(ENCODER_PIN_B);

        if (sampled_state != raw_state) {
            raw_state = sampled_state;
            raw_changed_us = now_us;
        }

        if (raw_state != stable_state && raw_state != 0 &&
            now_us - raw_changed_us >= ENCODER_DEBOUNCE_US) {
            const int8_t logical_step =
                transition_table[(stable_state << 2) | raw_state];
            if (logical_step != 0) {
                s_encoder_position += logical_step;
                ESP_LOGI(TAG, "ENC STEP %s, count=%ld, AB=%u%u",
                         logical_step > 0 ? "RIGHT" : "LEFT",
                         (long)s_encoder_position,
                         (raw_state >> 1) & 1U, raw_state & 1U);
            } else {
                ESP_LOGW(TAG, "ENC unexpected transition %u%u -> %u%u",
                         (stable_state >> 1) & 1U, stable_state & 1U,
                         (raw_state >> 1) & 1U, raw_state & 1U);
            }
            stable_state = raw_state;
            s_encoder_ab = stable_state;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

esp_err_t copet_encoder_start(void)
{
    const BaseType_t task_created =
        xTaskCreate(encoder_task, "encoder", 2048, NULL, 5, NULL);
    return task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

int32_t copet_encoder_position(void)
{
    return s_encoder_position;
}

uint8_t copet_encoder_ab(void)
{
    return s_encoder_ab;
}
