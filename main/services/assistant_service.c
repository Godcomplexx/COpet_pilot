#include "services/assistant_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "services/weather_service.h"

#if defined(CONFIG_COPET_ASSISTANT_BACKEND_HTTP) || \
    defined(CONFIG_COPET_ASSISTANT_BACKEND_OLLAMA)
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#define COPET_ASSISTANT_HAS_HTTP 1
#endif

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
static char s_req_text[ASSISTANT_QUERY_MAX];
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

#if defined(COPET_ASSISTANT_HAS_HTTP)

typedef struct {
    char bytes[2048]; /* contract caps a response at 2 KB */
    size_t length;
    bool overflowed;
} http_response_t;

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }
    http_response_t *response = event->user_data;
    if (response == NULL) { return ESP_ERR_INVALID_ARG; }
    const size_t free_bytes = sizeof(response->bytes) - 1U - response->length;
    if ((size_t)event->data_len > free_bytes) {
        response->overflowed = true;
        return ESP_ERR_NO_MEM;
    }
    memcpy(response->bytes + response->length, event->data,
           (size_t)event->data_len);
    response->length += (size_t)event->data_len;
    response->bytes[response->length] = '\0';
    return ESP_OK;
}

#if defined(CONFIG_COPET_ASSISTANT_BACKEND_HTTP)
/* Real backend: POST {endpoint}/v1/query and read {text, mood} from the reply
 * (see docs/architecture/cloud_api_contract.md). Returns ESP_OK with the answer
 * text filled, or an error on transport/parse failure. */
static esp_err_t http_query(const char *type, const char *text,
                            uint32_t request_id, char *out_text,
                            size_t out_text_cap, char *out_mood,
                            size_t out_mood_cap)
{
    char url[256];
    const int url_len = snprintf(url, sizeof(url), "%s/v1/query",
                                 CONFIG_COPET_ASSISTANT_ENDPOINT);
    if (url_len < 0 || (size_t)url_len >= sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }

    cJSON *request = cJSON_CreateObject();
    if (request == NULL) { return ESP_ERR_NO_MEM; }
    char id_text[12];
    snprintf(id_text, sizeof(id_text), "%u", (unsigned)request_id);
    cJSON_AddStringToObject(request, "request_id", id_text);
    cJSON_AddStringToObject(request, "type", type != NULL ? type : "query");
    cJSON_AddStringToObject(request, "text", text != NULL ? text : "");
    cJSON_AddStringToObject(request, "locale", "ru-RU");
    cJSON_AddStringToObject(request, "timezone", CONFIG_COPET_TIMEZONE);
    char *body = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);
    if (body == NULL) { return ESP_ERR_NO_MEM; }

    http_response_t response = {0};
    const esp_http_client_config_t configuration = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &response,
        .timeout_ms = 10000, /* contract: 10 s for text */
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&configuration);
    if (client == NULL) { free(body); return ESP_ERR_NO_MEM; }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, (int)strlen(body));

    const esp_err_t perform_result = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(body);

    if (perform_result != ESP_OK) { return perform_result; }
    if (status_code / 100 != 2 || response.length == 0 ||
        response.overflowed) {
        ESP_LOGW(TAG, "assistant HTTP status=%d bytes=%u", status_code,
                 (unsigned)response.length);
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *root = cJSON_ParseWithLength(response.bytes, response.length);
    if (root == NULL) { return ESP_ERR_INVALID_RESPONSE; }
    cJSON *answer = cJSON_GetObjectItemCaseSensitive(root, "text");
    cJSON *mood = cJSON_GetObjectItemCaseSensitive(root, "mood");
    esp_err_t result = ESP_ERR_INVALID_RESPONSE;
    if (cJSON_IsString(answer) && answer->valuestring != NULL) {
        copy_bounded(out_text, out_text_cap, answer->valuestring);
        copy_bounded(out_mood, out_mood_cap,
                     cJSON_IsString(mood) && mood->valuestring != NULL
                         ? mood->valuestring
                         : "");
        result = ESP_OK;
    }
    cJSON_Delete(root);
    return result;
}
#endif /* CONFIG_COPET_ASSISTANT_BACKEND_HTTP */

#if defined(CONFIG_COPET_ASSISTANT_BACKEND_OLLAMA)
/* Local Ollama: POST {url}/api/generate and read the model's `response`. The
 * system prompt keeps the reply short and ASCII so it renders on the card. */
static esp_err_t ollama_query(const char *text, char *out_text,
                              size_t out_text_cap, char *out_mood,
                              size_t out_mood_cap)
{
    char url[256];
    const int url_len = snprintf(url, sizeof(url), "%s/api/generate",
                                 CONFIG_COPET_ASSISTANT_OLLAMA_URL);
    if (url_len < 0 || (size_t)url_len >= sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }

    cJSON *request = cJSON_CreateObject();
    if (request == NULL) { return ESP_ERR_NO_MEM; }
    cJSON_AddStringToObject(request, "model",
                            CONFIG_COPET_ASSISTANT_OLLAMA_MODEL);
    cJSON_AddStringToObject(
        request, "system",
        "You are CoPet, a tiny desk robot. Reply with ONE short sentence, "
        "UPPERCASE ENGLISH LETTERS AND DIGITS ONLY, at most 12 words.");
    cJSON_AddStringToObject(request, "prompt", text != NULL ? text : "");
    cJSON_AddBoolToObject(request, "stream", 0);
    cJSON *options = cJSON_CreateObject();
    if (options != NULL) {
        cJSON_AddNumberToObject(options, "num_predict", 48);
        cJSON_AddItemToObject(request, "options", options);
    }
    char *body = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);
    if (body == NULL) { return ESP_ERR_NO_MEM; }

    http_response_t response = {0};
    const esp_http_client_config_t configuration = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &response,
        .timeout_ms = 30000, /* a local LLM can take a while to answer */
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&configuration);
    if (client == NULL) { free(body); return ESP_ERR_NO_MEM; }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, (int)strlen(body));

    const esp_err_t perform_result = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(body);

    if (perform_result != ESP_OK) { return perform_result; }
    if (status_code / 100 != 2 || response.length == 0 ||
        response.overflowed) {
        ESP_LOGW(TAG, "ollama HTTP status=%d bytes=%u", status_code,
                 (unsigned)response.length);
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *root = cJSON_ParseWithLength(response.bytes, response.length);
    if (root == NULL) { return ESP_ERR_INVALID_RESPONSE; }
    cJSON *answer = cJSON_GetObjectItemCaseSensitive(root, "response");
    esp_err_t result = ESP_ERR_INVALID_RESPONSE;
    if (cJSON_IsString(answer) && answer->valuestring != NULL) {
        copy_bounded(out_text, out_text_cap, answer->valuestring);
        copy_bounded(out_mood, out_mood_cap, "helpful");
        result = ESP_OK;
    }
    cJSON_Delete(root);
    return result;
}
#endif /* CONFIG_COPET_ASSISTANT_BACKEND_OLLAMA */
#endif /* COPET_ASSISTANT_HAS_HTTP */

/* Produce an answer via the configured backend. Returns ESP_OK with out_text
 * filled, or an error the caller surfaces as ASSISTANT_SERVICE_ERROR. */
static esp_err_t answer_query(const char *type, const char *text,
                             uint32_t request_id, char *out_text,
                             size_t out_text_cap, char *out_mood,
                             size_t out_mood_cap)
{
    /* Weather and time are local skills: answer them from real on-device data
     * even when a remote backend is configured (a cloud/LLM need not know the
     * actual room/clock values). Only free-form queries go to the backend. */
    if (type != NULL &&
        (strcmp(type, "weather") == 0 || strcmp(type, "time") == 0)) {
        (void)text;
        (void)request_id;
        stub_answer(type, out_text, out_text_cap, out_mood, out_mood_cap);
        return ESP_OK;
    }
#if defined(CONFIG_COPET_ASSISTANT_BACKEND_HTTP)
    if (strlen(CONFIG_COPET_ASSISTANT_ENDPOINT) > 0) {
        return http_query(type, text, request_id, out_text, out_text_cap,
                          out_mood, out_mood_cap);
    }
#elif defined(CONFIG_COPET_ASSISTANT_BACKEND_OLLAMA)
    if (strlen(CONFIG_COPET_ASSISTANT_OLLAMA_URL) > 0) {
        return ollama_query(text, out_text, out_text_cap, out_mood,
                            out_mood_cap);
    }
#endif
    (void)text;
    (void)request_id;
    stub_answer(type, out_text, out_text_cap, out_mood, out_mood_cap);
    return ESP_OK;
}

static void assistant_task(void *argument)
{
    (void)argument;
    while (true) {
        xSemaphoreTake(s_wake, portMAX_DELAY);

        char type[ASSISTANT_TYPE_MAX];
        char query[ASSISTANT_QUERY_MAX];
        uint32_t request_id = 0;
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        if (!s_have_request) {
            xSemaphoreGive(s_mutex);
            continue;
        }
        s_have_request = false;
        copy_bounded(type, sizeof(type), s_req_type);
        copy_bounded(query, sizeof(query), s_req_text);
        request_id = s_req_id;
        xSemaphoreGive(s_mutex);

        ESP_LOGI(TAG, "query #%u type=%s", (unsigned)request_id, type);

#if !defined(CONFIG_COPET_ASSISTANT_BACKEND_HTTP)
        /* Stub: simulate a round-trip so the WAITING state is visible. */
        vTaskDelay(pdMS_TO_TICKS(ASSISTANT_STUB_DELAY_MS));
#endif

        char text[sizeof(s_snapshot.text)];
        char mood[sizeof(s_snapshot.mood)];
        const esp_err_t result = answer_query(type, query, request_id, text,
                                              sizeof(text), mood, sizeof(mood));

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        if (result == ESP_OK) {
            s_snapshot.status = ASSISTANT_SERVICE_READY;
            s_snapshot.has_answer = true;
            copy_bounded(s_snapshot.text, sizeof(s_snapshot.text), text);
            copy_bounded(s_snapshot.mood, sizeof(s_snapshot.mood), mood);
        } else {
            s_snapshot.status = ASSISTANT_SERVICE_ERROR;
            s_snapshot.has_answer = false;
            copy_bounded(s_snapshot.text, sizeof(s_snapshot.text),
                         "SERVICE UNAVAILABLE");
            s_snapshot.mood[0] = '\0';
        }
        s_snapshot.request_id = request_id;
        ++s_snapshot.revision;
        xSemaphoreGive(s_mutex);
        ESP_LOGI(TAG, "answer #%u %s", (unsigned)request_id,
                 result == ESP_OK ? "ready" : "failed");
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
    if (!s_started) { return ESP_ERR_INVALID_STATE; }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_snapshot.status == ASSISTANT_SERVICE_FETCHING) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE; /* one request at a time */
    }
    copy_bounded(s_req_type, sizeof(s_req_type), type);
    copy_bounded(s_req_text, sizeof(s_req_text), text);
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
