#ifndef COPET_ENCODER_H
#define COPET_ENCODER_H

#include <stdint.h>

#include "esp_err.h"

/*
 * Rotary encoder from a three-contact mouse wheel (A=32, B=33, COM=GND).
 *
 * A background task polls and debounces the quadrature and accumulates a signed
 * detent count. The main loop reads the position and compares it to what it last
 * saw to detect steps.
 */

/* Configure the GPIOs and start the polling task. */
esp_err_t copet_encoder_start(void);

/* Signed accumulated detent count (increments right, decrements left). */
int32_t copet_encoder_position(void);

/* Latest debounced A/B line state (bit1=A, bit0=B), for diagnostics. */
uint8_t copet_encoder_ab(void);

#endif
