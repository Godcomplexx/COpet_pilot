#include "services/copet_nvs.h"

#include "nvs.h"
#include "nvs_flash.h"

#define COPET_NVS_NAMESPACE "copet"

esp_err_t copet_nvs_init(void)
{
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
        result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        const esp_err_t erase = nvs_flash_erase();
        if (erase != ESP_OK) { return erase; }
        result = nvs_flash_init();
    }
    return result;
}

bool copet_nvs_get_str(const char *key, char *out, size_t out_capacity)
{
    if (key == NULL || out == NULL || out_capacity == 0U) { return false; }
    nvs_handle_t handle;
    if (nvs_open(COPET_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    size_t length = out_capacity;
    const esp_err_t result = nvs_get_str(handle, key, out, &length);
    nvs_close(handle);
    return result == ESP_OK && out[0] != '\0';
}

esp_err_t copet_nvs_set_str(const char *key, const char *value)
{
    if (key == NULL || value == NULL) { return ESP_ERR_INVALID_ARG; }
    nvs_handle_t handle;
    esp_err_t result = nvs_open(COPET_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) { return result; }
    result = nvs_set_str(handle, key, value);
    if (result == ESP_OK) { result = nvs_commit(handle); }
    nvs_close(handle);
    return result;
}

uint8_t copet_nvs_get_u8(const char *key, uint8_t fallback)
{
    if (key == NULL) { return fallback; }
    nvs_handle_t handle;
    if (nvs_open(COPET_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return fallback;
    }
    uint8_t value = fallback;
    if (nvs_get_u8(handle, key, &value) != ESP_OK) { value = fallback; }
    nvs_close(handle);
    return value;
}

esp_err_t copet_nvs_set_u8(const char *key, uint8_t value)
{
    if (key == NULL) { return ESP_ERR_INVALID_ARG; }
    nvs_handle_t handle;
    esp_err_t result = nvs_open(COPET_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) { return result; }
    result = nvs_set_u8(handle, key, value);
    if (result == ESP_OK) { result = nvs_commit(handle); }
    nvs_close(handle);
    return result;
}
