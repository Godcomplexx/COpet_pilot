#include "services/wifi_provisioning.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "services/copet_nvs.h"
#include "services/wifi_credentials.h"

static const char *TAG = "copet_provision";

#define PROVISION_AP_SSID "CoPet-Setup"

static httpd_handle_t s_server;
static esp_timer_handle_t s_reboot_timer;

static const char SETUP_PAGE[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>CoPet Setup</title></head>"
    "<body style='font-family:sans-serif;max-width:420px;margin:24px auto;"
    "padding:0 14px'>"
    "<h2>CoPet Wi-Fi Setup</h2>"
    "<form method=POST action=/save>"
    "<p>Network name (SSID):<br>"
    "<input name=ssid maxlength=32 style='width:100%;padding:6px'></p>"
    "<p>Password:<br>"
    "<input name=password type=password maxlength=63 "
    "style='width:100%;padding:6px'></p>"
    "<p><button type=submit style='padding:8px 16px'>Save &amp; Reboot"
    "</button></p></form></body></html>";

/* In-place URL-decode: %XX -> byte, '+' -> space. */
static void url_decode(char *text)
{
    char *out = text;
    for (const char *in = text; *in != '\0'; ++in) {
        if (*in == '+') {
            *out++ = ' ';
        } else if (*in == '%' && in[1] != '\0' && in[2] != '\0') {
            char hex[3] = {in[1], in[2], '\0'};
            *out++ = (char)strtol(hex, NULL, 16);
            in += 2;
        } else {
            *out++ = *in;
        }
    }
    *out = '\0';
}

static void reboot_timer_callback(void *argument)
{
    (void)argument;
    ESP_LOGI(TAG, "Rebooting to apply Wi-Fi credentials");
    esp_restart();
}

static esp_err_t root_get_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html");
    return httpd_resp_send(request, SETUP_PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t save_post_handler(httpd_req_t *request)
{
    char body[256];
    int total = request->content_len;
    if (total <= 0 || total >= (int)sizeof(body)) {
        total = (int)sizeof(body) - 1;
    }
    const int received = httpd_req_recv(request, body, total);
    if (received <= 0) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "no body");
        return ESP_FAIL;
    }
    body[received] = '\0';

    char ssid[WIFI_SSID_CAPACITY] = {0};
    char password[WIFI_PASSWORD_CAPACITY] = {0};
    (void)httpd_query_key_value(body, "ssid", ssid, sizeof(ssid));
    (void)httpd_query_key_value(body, "password", password, sizeof(password));
    url_decode(ssid);
    url_decode(password);

    if (ssid[0] == '\0') {
        httpd_resp_set_type(request, "text/html");
        return httpd_resp_sendstr(
            request, "<html><body><h3>SSID is required. Go back.</h3>"
                     "</body></html>");
    }

    (void)copet_nvs_set_str("wifi_ssid", ssid);
    (void)copet_nvs_set_str("wifi_pass", password);
    ESP_LOGI(TAG, "Saved Wi-Fi credentials for '%s' via SoftAP", ssid);

    httpd_resp_set_type(request, "text/html");
    (void)httpd_resp_sendstr(
        request, "<html><body style='font-family:sans-serif'>"
                 "<h2>Saved.</h2><p>CoPet is rebooting to connect...</p>"
                 "</body></html>");

    const esp_timer_create_args_t timer_args = {
        .callback = reboot_timer_callback,
        .name = "provision_reboot",
    };
    if (s_reboot_timer == NULL) {
        (void)esp_timer_create(&timer_args, &s_reboot_timer);
    }
    if (s_reboot_timer != NULL) {
        (void)esp_timer_start_once(s_reboot_timer, 1500000ULL); /* 1.5 s */
    }
    return ESP_OK;
}

static esp_err_t start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    if (httpd_start(&s_server, &config) != ESP_OK) {
        return ESP_FAIL;
    }
    const httpd_uri_t root = {
        .uri = "/", .method = HTTP_GET, .handler = root_get_handler,
    };
    const httpd_uri_t save = {
        .uri = "/save", .method = HTTP_POST, .handler = save_post_handler,
    };
    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &save);
    return ESP_OK;
}

esp_err_t wifi_provisioning_start(void)
{
    if (esp_netif_create_default_wifi_ap() == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wifi_config_t ap_config = {0};
    memcpy(ap_config.ap.ssid, PROVISION_AP_SSID, strlen(PROVISION_AP_SSID));
    ap_config.ap.ssid_len = (uint8_t)strlen(PROVISION_AP_SSID);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 2;
    ap_config.ap.authmode = WIFI_AUTH_OPEN; /* open so setup is easy to join */

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG,
                        "AP mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG,
                        "AP config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "AP start failed");

    const esp_err_t server_result = start_http_server();
    if (server_result != ESP_OK) {
        ESP_LOGE(TAG, "Setup HTTP server failed to start");
        return server_result;
    }

    ESP_LOGI(TAG, "SoftAP '%s' up — connect and open http://192.168.4.1",
             PROVISION_AP_SSID);
    return ESP_OK;
}
