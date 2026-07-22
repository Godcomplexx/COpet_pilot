#include "services/assistant_service.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "services/weather_service.h"

enum {
    ASSISTANT_TASK_STACK_SIZE = 3072,
    ASSISTANT_STUB_DELAY_MS = 1200, /* fake "network" round-trip */
    ASSISTANT_TYPE_MAX = 16,
    ASSISTANT_QUERY_MAX = 128,
};

static const char *TAG = "copet_assistant";

static assistant_service_snapshot_t s_snapshot = {
    .status = ASSISTANT_SERVICE_IDLE,
};
static SemaphoreHandle_t s_mutex;
static SemaphoreHandle_t s_wake;
static bool s_started;

/* Pending request, guarded by s_mutex. */
static bool s_have_request;
static char s_req_type[ASSISTANT_TYPE_MAX];
static uint32_t s_req_id;

static void copy_bounded(char *dst, size_t cap, const char *src)
{
    if (cap == 0U) { return; }
    if (src == NULL) { dst[0] = '\0'; return; }
    size_t i = 0;
    for (; i + 1U < cap && src[i] != '\0'; ++i) { dst[i] = src[i]; }
    dst[i] = '\0';
}

static const char *weather_code_text(int code)
{
    if (code == 0) { return "CLEAR"; }
    if (code >= 1 && code <= 3) { return "CLOUDY"; }
    if (code == 45 || code == 48) { return "FOG"; }
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
        return "RAIN";
    }
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
        return "SNOW";
    }
    if (code >= 95) { return "STORM"; }
    return "CLOUDY";
}

/* Stub "backend": for the local skills (weather/time) it answers from real
 * on-device data so the card matches what CoPet then speaks. ASCII so the
 * uppercase font can render it. */
static void stub_answer(const char *type, char *text, size_t text_cap,
                        char *mood, size_t mood_cap)
{
    if (type != NULL && strcmp(type, "weather") == 0) {
        weather_service_snapshot_t weather;
        weather_service_get_snapshot(&weather);
        if (weather.has_data) {
            const int temp = (int)(weather.temperature_c +
                                   (weather.temperature_c >= 0.0f ? 0.5f
                                                                   : -0.5f));
            char line[64];
            snprintf(line, sizeof(line), "%d DEGREES, %s", temp,
                     weather_code_text(weather.weather_code));
            copy_bounded(text, text_cap, line);
        } else {
            copy_bounded(text, text_cap, "WEATHER NOT READY YET.");
        }
        copy_bounded(mood, mood_cap, "helpful");
    } else if (type != NULL && strcmp(type, "time") == 0) {
        const time_t now = time(NULL);
        struct tm local;
        localtime_r(&now, &local);
        if (local.tm_year + 1900 >= 2021) {
            char line[32];
            snprintf(line, sizeof(line), "IT IS %02d:%02d", local.tm_hour,
                     local.tm_min);
            copy_bounded(text, text_cap, line);
        } else {
            copy_bounded(text, text_cap, "TIME NOT SYNCED YET.");
        }
        copy_bounded(mood, mood_cap, "neutral");
    } else {
        copy_bounded(text, text_cap, "HI! I AM COPET, YOUR DESK PET.");
        copy_bounded(mood, mood_cap, "happy");
    }
}

static void assistant_task(void *argument)
{
    (void)argument;
    while (true) {
        xSemaphoreTake(s_wake, portMAX_DELAY);

        char type[ASSISTANT_TYPE_MAX];
        uint32_t request_id = 0;
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        if (!s_have_request) {
            xSemaphoreGive(s_mutex);
            continue;
        }
        s_have_request = false;
        copy_bounded(type, sizeof(type), s_req_type);
        request_id = s_req_id;
        xSemaphoreGive(s_mutex);

        ESP_LOGI(TAG, "query #%u type=%s (stub)", (unsigned)request_id, type);

        /* Simulate the round-trip without blocking the UI task. */
        vTaskDelay(pdMS_TO_TICKS(ASSISTANT_STUB_DELAY_MS));

        char text[sizeof(s_snapshot.text)];
        char mood[sizeof(s_snapshot.mood)];
        stub_answer(type, text, sizeof(text), mood, sizeof(mood));

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_snapshot.status = ASSISTANT_SERVICE_READY;
        s_snapshot.has_answer = true;
        copy_bounded(s_snapshot.text, sizeof(s_snapshot.text), text);
        copy_bounded(s_snapshot.mood, sizeof(s_snapshot.mood), mood);
        s_snapshot.request_id = request_id;
        ++s_snapshot.revision;
        xSemaphoreGive(s_mutex);
        ESP_LOGI(TAG, "answer #%u ready", (unsigned)request_id);
    }
}

esp_err_t assistant_service_start(void)
{
    if (s_started) { return ESP_OK; }

    s_mutex = xSemaphoreCreateMutex();
    s_wake = xSemaphoreCreateBinary();
    if (s_mutex == NULL || s_wake == NULL) { return ESP_ERR_NO_MEM; }

    const BaseType_t created = xTaskCreate(
        assistant_task, "copet_assistant", ASSISTANT_TASK_STACK_SIZE,
        NULL, 4, NULL);
    if (created != pdPASS) { return ESP_ERR_NO_MEM; }

    s_started = true;
    return ESP_OK;
}

esp_err_t assistant_service_submit(const char *type, const char *text)
{
    (void)text; /* the stub answers from the type; a real backend sends text */
    if (!s_started) { return ESP_ERR_INVALID_STATE; }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_snapshot.status == ASSISTANT_SERVICE_FETCHING) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE; /* one request at a time */
    }
    copy_bounded(s_req_type, sizeof(s_req_type), type);
    s_req_id = s_snapshot.request_id + 1U;
    s_have_request = true;
    s_snapshot.status = ASSISTANT_SERVICE_FETCHING;
    s_snapshot.has_answer = false;
    s_snapshot.text[0] = '\0';
    s_snapshot.mood[0] = '\0';
    s_snapshot.request_id = s_req_id;
    ++s_snapshot.revision;
    xSemaphoreGive(s_mutex);

    xSemaphoreGive(s_wake);
    return ESP_OK;
}

void assistant_service_get_snapshot(assistant_service_snapshot_t *snapshot)
{
    if (snapshot == NULL) { return; }
    if (s_mutex != NULL) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        *snapshot = s_snapshot;
        xSemaphoreGive(s_mutex);
        return;
    }
    *snapshot = s_snapshot;
}

const char *assistant_service_status_label(assistant_service_status_t status)
{
    switch (status) {
    case ASSISTANT_SERVICE_FETCHING: return "FETCHING";
    case ASSISTANT_SERVICE_READY: return "READY";
    case ASSISTANT_SERVICE_ERROR: return "ERROR";
    case ASSISTANT_SERVICE_IDLE:
    default: return "IDLE";
    }
}
