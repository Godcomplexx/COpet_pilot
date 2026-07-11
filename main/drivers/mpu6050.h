#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

typedef struct {
    float accel_x_g;
    float accel_y_g;
    float accel_z_g;
    float gyro_x_dps;
    float gyro_y_dps;
    float gyro_z_dps;
    float temperature_c;
} mpu6050_sample_t;

/*
 * Finds an MPU6050 at 0x68 or 0x69, verifies WHO_AM_I, and wakes the sensor.
 */
esp_err_t mpu6050_init(i2c_master_bus_handle_t bus, uint8_t *address);

esp_err_t mpu6050_read(mpu6050_sample_t *sample);
