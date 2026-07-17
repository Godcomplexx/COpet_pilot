#include "services/weather_service.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "services/wifi_service.h"

enum {
    WEATHER_RESPONSE_CAPACITY = 1024,
    WEATHER_TASK_STACK_SIZE = 6144,
    WEATHER_REFRESH_SECONDS = 15 * 60,
    WEATHER_RETRY_SECONDS = 60,
    TIME_SYNC_TIMEOUT_SECONDS = 15,
};

typedef struct {
    char bytes[WEATHER_RESPONSE_CAPACITY];
    size_t length;
    bool overflowed;
} weather_response_t;

static const char *TAG = "copet_weather";
static SemaphoreHandle_t s_mutex;
static weather_service_snapshot_t s_snapshot = {
    .status = WEATHER_SERVICE_OFF,
};
static bool s_started;
static bool s_time_service_started;
static bool s_time_synchronized;

static void set_status(weather_service_status_t status)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_snapshot.status != status) {
        s_snapshot.status = status;
        ++s_snapshot.revision;
    }
    xSemaphoreGive(s_mutex);
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }

    weather_response_t *response = event->user_data;
    if (response == NULL) { return ESP_ERR_INVALID_ARG; }

    const size_t free_bytes = sizeof(response->bytes) - 1U - response->length;
    if ((size_t)event->data_len > free_bytes) {
        response->overflowed = true;
        return ESP_ERR_NO_MEM;
    }

    memcpy(response->bytes + response->length,
           event->data, (size_t)event->data_len);
    response->length += (size_t)event->data_len;
    response->bytes[response->length] = '\0';
    return ESP_OK;
}

static esp_err_t parse_current_weather(
    const char *json, size_t length,
    float *temperature_c, float *humidity_percent, int *weather_code)
{
    cJSON *root = cJSON_ParseWithLength(json, length);
    if (root == NULL) { return ESP_ERR_INVALID_RESPONSE; }

    cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    cJSON *temperature = current == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(current, "temperature_2m");
    cJSON *humidity = current == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(current, "relative_humidity_2m");
    cJSON *code = current == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(current, "weather_code");

    const bool valid = cJSON_IsNumber(temperature) &&
                       cJSON_IsNumber(humidity) && cJSON_IsNumber(code);
    if (valid) {
        *temperature_c = (float)temperature->valuedouble;
        *humidity_percent = (float)humidity->valuedouble;
        *weather_code = code->valueint;
    }
    cJSON_Delete(root);
    return valid ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t synchronize_time(void)
{
    if (s_time_synchronized) { return ESP_OK; }

    if (!s_time_service_started) {
        const esp_sntp_config_t configuration =
            ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        const esp_err_t init_result =
            esp_netif_sntp_init(&configuration);
        if (init_result != ESP_OK) { return init_result; }
        s_time_service_started = true;
        ESP_LOGI(TAG, "Waiting for SNTP time");
    }

    const esp_err_t sync_result = esp_netif_sntp_sync_wait(
        pdMS_TO_TICKS(TIME_SYNC_TIMEOUT_SECONDS * 1000));
    if (sync_result != ESP_OK) { return sync_result; }

    time_t now = 0;
    time(&now);
    s_time_synchronized = now > 1700000000;
    if (!s_time_synchronized) { return ESP_ERR_INVALID_STATE; }
    ESP_LOGI(TAG, "SNTP time synchronized, unix=%lld", (long long)now);
    return ESP_OK;
}

static esp_err_t fetch_current_weather(float *temperature_c,
                                       float *humidity_percent,
                                       int *weather_code)
{
    char url[320];
    const int url_length = snprintf(
        url, sizeof(url),
        "http://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
        "&current=temperature_2m,relative_humidity_2m,weather_code"
        "&timezone=auto&forecast_days=1",
        CONFIG_COPET_WEATHER_LATITUDE, CONFIG_COPET_WEATHER_LONGITUDE);
    if (url_length < 0 || (size_t)url_length >= sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }

    weather_response_t response = {0};
    const esp_http_client_config_t configuration = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &response,
        .timeout_ms = 20000,
        .buffer_size = 512,
        .buffer_size_tx = 512,
    };
    esp_http_client_handle_t client =
        esp_http_client_init(&configuration);
    if (client == NULL) { return ESP_ERR_NO_MEM; }

    ESP_LOGI(TAG, "Weather HTTP start: free heap=%u, largest block=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    const esp_err_t perform_result = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (perform_result != ESP_OK) { return perform_result; }
    if (status_code != 200 || response.overflowed || response.length == 0) {
        ESP_LOGW(TAG, "Weather HTTP response: status=%d, bytes=%u",
                 status_code, (unsigned)response.length);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return parse_current_weather(response.bytes, response.length,
                                 temperature_c, humidity_percent,
                                 weather_code);
}

static void weather_task(void *argument)
{
    (void)argument;
    TickType_t wait_ticks = pdMS_TO_TICKS(1000);

    while (true) {
        if (!wifi_service_is_connected()) {
            set_status(WEATHER_SERVICE_WAITING_WIFI);
            wait_ticks = pdMS_TO_TICKS(1000);
        } else {
            set_status(WEATHER_SERVICE_FETCHING);
            float temperature_c = 0.0f;
            float humidity_percent = 0.0f;
            int weather_code = 0;
            esp_err_t result = synchronize_time();
            if (result == ESP_OK) {
                result = fetch_current_weather(
                    &temperature_c, &humidity_percent, &weather_code);
            }
            if (result == ESP_OK) {
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                s_snapshot.status = WEATHER_SERVICE_READY;
                s_snapshot.has_data = true;
                s_snapshot.temperature_c = temperature_c;
                s_snapshot.humidity_percent = humidity_percent;
                s_snapshot.weather_code = weather_code;
                ++s_snapshot.revision;
                xSemaphoreGive(s_mutex);
                ESP_LOGI(TAG,
                         "Outdoor weather: temperature=%.1f C, humidity=%.0f %%, code=%d",
                         (double)temperature_c, (double)humidity_percent,
                         weather_code);
                wait_ticks = pdMS_TO_TICKS(WEATHER_REFRESH_SECONDS * 1000);
            } else {
                set_status(WEATHER_SERVICE_ERROR);
                ESP_LOGW(TAG, "Weather request failed: %s",
                         esp_err_to_name(result));
                wait_ticks = pdMS_TO_TICKS(WEATHER_RETRY_SECONDS * 1000);
            }
        }
        vTaskDelay(wait_ticks);
    }
}

esp_err_t weather_service_start(void)
{
    if (s_started) { return ESP_OK; }
    s_started = true;

#if !CONFIG_COPET_WEATHER_ENABLED
    return ESP_OK;
#else
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) { return ESP_ERR_NO_MEM; }
    set_status(WEATHER_SERVICE_WAITING_WIFI);

    const BaseType_t created = xTaskCreate(
        weather_task, "copet_weather", WEATHER_TASK_STACK_SIZE,
        NULL, 4, NULL);
    if (created != pdPASS) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Outdoor weather enabled for %s, %s",
             CONFIG_COPET_WEATHER_LATITUDE,
             CONFIG_COPET_WEATHER_LONGITUDE);
    return ESP_OK;
#endif
}

void weather_service_get_snapshot(weather_service_snapshot_t *snapshot)
{
    if (snapshot == NULL) { return; }
    if (s_mutex == NULL) {
        *snapshot = s_snapshot;
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *snapshot = s_snapshot;
    xSemaphoreGive(s_mutex);
}

const char *weather_service_status_label(weather_service_status_t status)
{
    switch (status) {
    case WEATHER_SERVICE_WAITING_WIFI: return "WAIT";
    case WEATHER_SERVICE_FETCHING: return "FETCH";
    case WEATHER_SERVICE_READY: return "READY";
    case WEATHER_SERVICE_ERROR: return "ERROR";
    case WEATHER_SERVICE_OFF:
    default: return "OFF";
    }
}
