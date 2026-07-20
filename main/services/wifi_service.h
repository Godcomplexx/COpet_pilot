#ifndef COPET_WIFI_SERVICE_H
#define COPET_WIFI_SERVICE_H

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    WIFI_SERVICE_OFF,
    WIFI_SERVICE_NO_CREDENTIALS,
    WIFI_SERVICE_STARTING,
    WIFI_SERVICE_CONNECTING,
    WIFI_SERVICE_RETRY_WAIT,
    WIFI_SERVICE_CONNECTED,
    WIFI_SERVICE_ERROR,
} wifi_service_status_t;

/* Starts Wi-Fi without waiting for a connection. Safe to call once at boot. */
esp_err_t wifi_service_start(void);

wifi_service_status_t wifi_service_get_status(void);
const char *wifi_service_status_label(wifi_service_status_t status);
bool wifi_service_is_connected(void);

#endif
