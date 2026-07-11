#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef enum {
    TOUCH_BUTTON_EVENT_NONE = 0,
    TOUCH_BUTTON_EVENT_SHORT,
    TOUCH_BUTTON_EVENT_LONG,
} touch_button_event_t;

esp_err_t touch_button_init(void);

/* Call at least every 20 ms. now_ms must be monotonic. */
touch_button_event_t touch_button_poll(int64_t now_ms);
