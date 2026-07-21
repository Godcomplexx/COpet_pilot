#ifndef COPET_I2C_H
#define COPET_I2C_H

#include "driver/i2c_master.h"
#include "esp_err.h"

/*
 * Shared I2C master bus (I2C_NUM_0, SDA=21, SCL=22) used by the on-board
 * sensors (SHT31 and MPU6050). Initialize it once at boot, then hand the bus
 * handle to each sensor driver's init.
 */

esp_err_t copet_i2c_init(void);

/* Bus handle, valid only after a successful copet_i2c_init(). */
i2c_master_bus_handle_t copet_i2c_bus(void);

#endif
