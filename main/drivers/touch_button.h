#pragma once

#include <stdbool.h>
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

/*
 * True while the pad is being held (debounced). Lets the app measure how long
 * a touch lasts — e.g. a "petting" gesture — without waiting for release.
 * touch_button_poll() must be called regularly to keep this state current.
 */
bool touch_button_is_pressed(void);
