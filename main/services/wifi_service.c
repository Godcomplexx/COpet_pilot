#include "services/wifi_service.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char *TAG = "copet_wifi";

static volatile wifi_service_status_t s_status = WIFI_SERVICE_OFF;
static volatile uint32_t s_retry_count;
static bool s_started;
static esp_timer_handle_t s_reconnect_timer;
static esp_event_handler_instance_t s_wifi_event_handler;
static esp_event_handler_instance_t s_ip_event_handler;

#define WIFI_RETURN_ON_ERROR(operation, message) do {                 \
    const esp_err_t wifi_result_ = (operation);                       \
    if (wifi_result_ != ESP_OK) {                                     \
        s_status = WIFI_SERVICE_ERROR;                                \
        ESP_LOGE(TAG, "%s: %s", message, esp_err_to_name(wifi_result_)); \
        return wifi_result_;                                          \
    }                                                                 \
} while (0)

static esp_err_t initialize_nvs(void)
{
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
        result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase failed");
        result = nvs_flash_init();
    }
    return result;
}

static void connect_now(void)
{
    s_status = WIFI_SERVICE_CONNECTING;
    const esp_err_t result = esp_wifi_connect();
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi connect request failed: %s",
                 esp_err_to_name(result));
        s_status = WIFI_SERVICE_ERROR;
    }
}

static void reconnect_timer_callback(void *argument)
{
    (void)argument;
    connect_now();
}

static void schedule_reconnect(void)
{
    const uint32_t capped_retry = s_retry_count < 5U ? s_retry_count : 5U;
    uint32_t delay_seconds = 1U << capped_retry;
    if (delay_seconds > 30U) { delay_seconds = 30U; }

    s_status = WIFI_SERVICE_RETRY_WAIT;
    (void)esp_timer_stop(s_reconnect_timer);
    const esp_err_t result = esp_timer_start_once(
        s_reconnect_timer, (uint64_t)delay_seconds * 1000000ULL);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi reconnect timer failed: %s",
                 esp_err_to_name(result));
        s_status = WIFI_SERVICE_ERROR;
        return;
    }
    ESP_LOGW(TAG, "Wi-Fi reconnect in %" PRIu32 " s", delay_seconds);
}

static void wifi_event_handler(void *argument, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)argument;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        connect_now();
        return;
    }
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = event_data;
        ++s_retry_count;
        ESP_LOGW(TAG, "Wi-Fi disconnected, reason=%u, attempt=%" PRIu32,
                 event != NULL ? event->reason : 0U, s_retry_count);
        schedule_reconnect();
        return;
    }
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        (void)esp_timer_stop(s_reconnect_timer);
        s_retry_count = 0;
        s_status = WIFI_SERVICE_CONNECTED;
        if (event != NULL) {
            ESP_LOGI(TAG, "Wi-Fi connected, address=" IPSTR,
                     IP2STR(&event->ip_info.ip));
        } else {
            ESP_LOGI(TAG, "Wi-Fi connected");
        }
    }
}

esp_err_t wifi_service_start(void)
{
    if (s_started) { return ESP_OK; }
    s_started = true;

#if !CONFIG_COPET_WIFI_ENABLED
    s_status = WIFI_SERVICE_OFF;
    ESP_LOGI(TAG, "Wi-Fi disabled in menuconfig");
    return ESP_OK;
#else
    if (CONFIG_COPET_WIFI_SSID[0] == '\0') {
        s_status = WIFI_SERVICE_NO_CREDENTIALS;
        ESP_LOGW(TAG,
                 "Wi-Fi is not configured; set CoPet Pilot > Wi-Fi SSID in menuconfig");
        return ESP_OK;
    }

    s_status = WIFI_SERVICE_STARTING;
    WIFI_RETURN_ON_ERROR(initialize_nvs(), "NVS initialization failed");

    esp_err_t result = esp_netif_init();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        s_status = WIFI_SERVICE_ERROR;
        return result;
    }
    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        s_status = WIFI_SERVICE_ERROR;
        return result;
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
        s_status = WIFI_SERVICE_ERROR;
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t initialization = WIFI_INIT_CONFIG_DEFAULT();
    WIFI_RETURN_ON_ERROR(esp_wifi_init(&initialization),
                         "Wi-Fi driver initialization failed");
    WIFI_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM),
                         "Wi-Fi RAM storage selection failed");

    const esp_timer_create_args_t timer_configuration = {
        .callback = reconnect_timer_callback,
        .name = "wifi_retry",
    };
    WIFI_RETURN_ON_ERROR(
        esp_timer_create(&timer_configuration, &s_reconnect_timer),
        "Wi-Fi reconnect timer creation failed");
    WIFI_RETURN_ON_ERROR(
        esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL,
            &s_wifi_event_handler),
        "Wi-Fi event handler registration failed");
    WIFI_RETURN_ON_ERROR(
        esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL,
            &s_ip_event_handler),
        "IP event handler registration failed");

    wifi_config_t station_configuration = {0};
    snprintf((char *)station_configuration.sta.ssid,
             sizeof(station_configuration.sta.ssid), "%s",
             CONFIG_COPET_WIFI_SSID);
    snprintf((char *)station_configuration.sta.password,
             sizeof(station_configuration.sta.password), "%s",
             CONFIG_COPET_WIFI_PASSWORD);
    station_configuration.sta.threshold.authmode =
        CONFIG_COPET_WIFI_PASSWORD[0] == '\0'
            ? WIFI_AUTH_OPEN
            : WIFI_AUTH_WPA2_PSK;
    station_configuration.sta.pmf_cfg.capable = true;
    station_configuration.sta.pmf_cfg.required = false;

    WIFI_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA),
                         "Wi-Fi Station mode failed");
    WIFI_RETURN_ON_ERROR(
        esp_wifi_set_config(WIFI_IF_STA, &station_configuration),
        "Wi-Fi Station configuration failed");
    WIFI_RETURN_ON_ERROR(esp_wifi_start(), "Wi-Fi start failed");

    ESP_LOGI(TAG, "Wi-Fi Station started for SSID '%s'",
             CONFIG_COPET_WIFI_SSID);
    return ESP_OK;
#endif
}

wifi_service_status_t wifi_service_get_status(void)
{
    return s_status;
}

const char *wifi_service_status_label(wifi_service_status_t status)
{
    switch (status) {
    case WIFI_SERVICE_NO_CREDENTIALS: return "WIFI SET";
    case WIFI_SERVICE_STARTING: return "WIFI INIT";
    case WIFI_SERVICE_CONNECTING: return "WIFI CONN";
    case WIFI_SERVICE_RETRY_WAIT: return "WIFI WAIT";
    case WIFI_SERVICE_CONNECTED: return "WIFI OK";
    case WIFI_SERVICE_ERROR: return "WIFI ERR";
    case WIFI_SERVICE_OFF:
    default: return "WIFI OFF";
    }
}

bool wifi_service_is_connected(void)
{
    return s_status == WIFI_SERVICE_CONNECTED;
}
