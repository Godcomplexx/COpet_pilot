#include "drivers/mpu6050.h"

#include <stdbool.h>
#include <stddef.h>

#include "esp_check.h"
#include "esp_log.h"

enum {
    MPU6050_ADDRESS_LOW = 0x68,
    MPU6050_ADDRESS_HIGH = 0x69,
    MPU6500_WHO_AM_I = 0x70,
    MPU6050_REG_ACCEL_XOUT_H = 0x3B,
    MPU6050_REG_PWR_MGMT_1 = 0x6B,
    MPU6050_REG_WHO_AM_I = 0x75,
};

static const char *TAG = "mpu6050";
static i2c_master_dev_handle_t s_device;

static esp_err_t read_register(i2c_master_dev_handle_t device,
                               uint8_t reg,
                               uint8_t *data,
                               size_t data_size)
{
    return i2c_master_transmit_receive(
        device, &reg, sizeof(reg), data, data_size, 100);
}

static esp_err_t write_register(i2c_master_dev_handle_t device,
                                uint8_t reg,
                                uint8_t value)
{
    const uint8_t command[] = {reg, value};
    return i2c_master_transmit(device, command, sizeof(command), 100);
}

static void log_i2c_scan(i2c_master_bus_handle_t bus)
{
    bool found = false;
    ESP_LOGW(TAG, "MPU6050 detection failed; scanning the I2C bus");
    for (uint16_t candidate = 0x08; candidate <= 0x77; ++candidate) {
        if (i2c_master_probe(bus, candidate, 20) == ESP_OK) {
            ESP_LOGW(TAG, "I2C device responds at 0x%02X", candidate);
            found = true;
        }
    }
    if (!found) {
        ESP_LOGW(TAG, "I2C scan found no devices");
    }
}

esp_err_t mpu6050_init(i2c_master_bus_handle_t bus, uint8_t *address)
{
    ESP_RETURN_ON_FALSE(bus != NULL && address != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "Invalid argument");

    const uint8_t addresses[] = {
        MPU6050_ADDRESS_LOW,
        MPU6050_ADDRESS_HIGH,
    };

    for (size_t index = 0; index < sizeof(addresses); ++index) {
        const i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addresses[index],
            .scl_speed_hz = 100000,
        };

        i2c_master_dev_handle_t candidate = NULL;
        esp_err_t result =
            i2c_master_bus_add_device(bus, &config, &candidate);
        if (result != ESP_OK) {
            continue;
        }

        uint8_t identity = 0;
        result = read_register(candidate, MPU6050_REG_WHO_AM_I,
                               &identity, sizeof(identity));
        if (result == ESP_OK && (identity == MPU6050_ADDRESS_LOW ||
                                 identity == MPU6050_ADDRESS_HIGH ||
                                 identity == MPU6500_WHO_AM_I)) {
            ESP_RETURN_ON_ERROR(
                write_register(candidate, MPU6050_REG_PWR_MGMT_1, 0x00),
                TAG, "Failed to wake MPU6050");
            s_device = candidate;
            *address = addresses[index];
            ESP_LOGI(TAG, "%s detected at 0x%02X, WHO_AM_I=0x%02X",
                     identity == MPU6500_WHO_AM_I
                         ? "MPU6500-compatible IMU"
                         : "MPU6050",
                     *address, identity);
            return ESP_OK;
        }

        if (result == ESP_OK) {
            ESP_LOGW(TAG,
                     "Device at 0x%02X has unexpected WHO_AM_I=0x%02X",
                     addresses[index], identity);
        } else {
            ESP_LOGW(TAG, "No MPU6050 response at 0x%02X: %s",
                     addresses[index], esp_err_to_name(result));
        }

        (void)i2c_master_bus_rm_device(candidate);
    }

    log_i2c_scan(bus);
    return ESP_ERR_NOT_FOUND;
}

static int16_t read_be_i16(const uint8_t *data)
{
    return (int16_t)(((uint16_t)data[0] << 8) | data[1]);
}

esp_err_t mpu6050_read(mpu6050_sample_t *sample)
{
    ESP_RETURN_ON_FALSE(sample != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "Sample is NULL");
    ESP_RETURN_ON_FALSE(s_device != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "MPU6050 is not initialized");

    uint8_t raw[14];
    ESP_RETURN_ON_ERROR(
        read_register(s_device, MPU6050_REG_ACCEL_XOUT_H,
                      raw, sizeof(raw)),
        TAG, "Sample read failed");

    sample->accel_x_g = read_be_i16(&raw[0]) / 16384.0f;
    sample->accel_y_g = read_be_i16(&raw[2]) / 16384.0f;
    sample->accel_z_g = read_be_i16(&raw[4]) / 16384.0f;
    sample->temperature_c = read_be_i16(&raw[6]) / 340.0f + 36.53f;
    sample->gyro_x_dps = read_be_i16(&raw[8]) / 131.0f;
    sample->gyro_y_dps = read_be_i16(&raw[10]) / 131.0f;
    sample->gyro_z_dps = read_be_i16(&raw[12]) / 131.0f;
    return ESP_OK;
}
