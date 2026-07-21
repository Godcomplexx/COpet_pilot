#include "drivers/copet_sht31.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SHT31_ADDR_PRIMARY 0x44
#define SHT31_ADDR_SECONDARY 0x45

static const char *TAG = "copet_sht31";

static i2c_master_dev_handle_t s_sht31_primary;
static i2c_master_dev_handle_t s_sht31_secondary;

esp_err_t copet_sht31_init(i2c_master_bus_handle_t bus)
{
    const i2c_device_config_t primary_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHT31_ADDR_PRIMARY,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(
        i2c_master_bus_add_device(bus, &primary_config, &s_sht31_primary),
        TAG, "SHT3x primary address setup failed");

    const i2c_device_config_t secondary_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHT31_ADDR_SECONDARY,
        .scl_speed_hz = 100000,
    };
    return i2c_master_bus_add_device(bus, &secondary_config,
                                     &s_sht31_secondary);
}

static uint8_t sht31_crc(const uint8_t *data)
{
    uint8_t crc = 0xFF;
    for (int byte = 0; byte < 2; ++byte) {
        crc ^= data[byte];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80U) ? (uint8_t)((crc << 1) ^ 0x31U)
                                : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static esp_err_t sht31_read_at(i2c_master_dev_handle_t device,
                               uint8_t address,
                               float *temperature,
                               float *humidity)
{
    const uint8_t measure_high_repeatability[] = {0x24, 0x00};
    uint8_t response[6];

    ESP_RETURN_ON_ERROR(
        i2c_master_transmit(device, measure_high_repeatability,
                            sizeof(measure_high_repeatability), 100),
        TAG, "SHT3x command failed at address 0x%02X", address);

    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_RETURN_ON_ERROR(
        i2c_master_receive(device, response, sizeof(response), 100),
        TAG, "SHT3x read failed at address 0x%02X", address);

    ESP_RETURN_ON_FALSE(
        sht31_crc(&response[0]) == response[2] &&
            sht31_crc(&response[3]) == response[5],
        ESP_ERR_INVALID_CRC, TAG, "SHT3x CRC check failed");

    const uint16_t raw_temperature =
        ((uint16_t)response[0] << 8) | response[1];
    const uint16_t raw_humidity =
        ((uint16_t)response[3] << 8) | response[4];

    *temperature = -45.0f + 175.0f * raw_temperature / 65535.0f;
    *humidity = 100.0f * raw_humidity / 65535.0f;
    return ESP_OK;
}

esp_err_t copet_sht31_read(float *temperature, float *humidity,
                           uint8_t *detected_address)
{
    if (*detected_address != 0) {
        return sht31_read_at(
            *detected_address == SHT31_ADDR_PRIMARY
                ? s_sht31_primary
                : s_sht31_secondary,
            *detected_address, temperature, humidity);
    }

    esp_err_t error = sht31_read_at(
        s_sht31_primary, SHT31_ADDR_PRIMARY, temperature, humidity);
    if (error == ESP_OK) {
        *detected_address = SHT31_ADDR_PRIMARY;
        return ESP_OK;
    }

    error = sht31_read_at(
        s_sht31_secondary, SHT31_ADDR_SECONDARY, temperature, humidity);
    if (error == ESP_OK) {
        *detected_address = SHT31_ADDR_SECONDARY;
    }
    return error;
}
