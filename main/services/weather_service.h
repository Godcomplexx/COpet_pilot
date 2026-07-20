#ifndef COPET_WEATHER_SERVICE_H
#define COPET_WEATHER_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    WEATHER_SERVICE_OFF,
    WEATHER_SERVICE_WAITING_WIFI,
    WEATHER_SERVICE_FETCHING,
    WEATHER_SERVICE_READY,
    WEATHER_SERVICE_ERROR,
} weather_service_status_t;

typedef struct {
    weather_service_status_t status;
    bool has_data;
    float temperature_c;
    float humidity_percent;
    int weather_code;
    uint32_t revision;
} weather_service_snapshot_t;

/* Starts a background task. It never blocks the Desk UI task. */
esp_err_t weather_service_start(void);
void weather_service_get_snapshot(weather_service_snapshot_t *snapshot);
const char *weather_service_status_label(weather_service_status_t status);

#endif
