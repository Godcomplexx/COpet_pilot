#ifndef COPET_DIAG_UI_H
#define COPET_DIAG_UI_H

#include <stdint.h>

#include "drivers/copet_ble.h"

/*
 * Diagnostic screens. These modes are no longer reachable from the product
 * menu; they stay available for bring-up and hardware checks only.
 */
void diag_ui_render_phone_bridge(uint8_t *framebuffer, int width, int height,
                                 copet_ble_status_t status,
                                 const char *received_message);
#endif
