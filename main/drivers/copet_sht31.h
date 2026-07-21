#ifndef COPET_SHT31_H
#define COPET_SHT31_H

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

/*
 * SHT31 temperature/humidity sensor on the shared I2C bus. The board may strap
 * the sensor to either 0x44 or 0x45, so both are registered and the first that
 * answers is remembered.
 */

/* Register the SHT31 devices (0x44 and 0x45) on an existing I2C bus. */
esp_err_t copet_sht31_init(i2c_master_bus_handle_t bus);

/*
 * Read temperature (C) and humidity (%). *detected_address caches which of
 * 0x44/0x45 answered: pass 0 to probe both, and it is set to the working
 * address on success. On failure the caller should reset it to 0 to re-probe.
 */
esp_err_t copet_sht31_read(float *temperature, float *humidity,
                           uint8_t *detected_address);

#endif
