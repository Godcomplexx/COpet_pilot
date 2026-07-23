#ifndef COPET_NVS_H
#define COPET_NVS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * Tiny key/value store in NVS namespace "copet" for persisted user state:
 * settings (sound, focus preset) and the Wi-Fi credential entered via SoftAP
 * provisioning. Keep keys <= 15 chars (NVS limit).
 */

/* Ensure the NVS flash partition is ready (safe to call more than once). */
esp_err_t copet_nvs_init(void);

/* Read a string; returns false if missing/empty or on error. */
bool copet_nvs_get_str(const char *key, char *out, size_t out_capacity);

/* Write a string (persisted immediately). */
esp_err_t copet_nvs_set_str(const char *key, const char *value);

/* Read a byte, or `fallback` if the key is missing. */
uint8_t copet_nvs_get_u8(const char *key, uint8_t fallback);

/* Write a byte (persisted immediately). */
esp_err_t copet_nvs_set_u8(const char *key, uint8_t value);

#endif
