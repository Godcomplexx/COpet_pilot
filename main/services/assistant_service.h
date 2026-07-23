#ifndef COPET_ASSISTANT_SERVICE_H
#define COPET_ASSISTANT_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * Assistant backend service (M7). Runs a background task and publishes an
 * answer snapshot, mirroring weather_service so the Desk UI never blocks.
 *
 * The current backend is a STUB: it returns a canned answer after a short
 * simulated delay, so the whole flow works with no cloud API or keys. A real
 * HTTPS `/v1/query` backend replaces the stub later (see docs/27).
 */

typedef enum {
    ASSISTANT_SERVICE_IDLE,
    ASSISTANT_SERVICE_FETCHING,
    ASSISTANT_SERVICE_READY,
    ASSISTANT_SERVICE_ERROR,
} assistant_service_status_t;

typedef struct {
    assistant_service_status_t status;
    bool has_answer;
    char text[192];   /* answer or error text (UTF-8; ASCII in the stub) */
    char mood[16];    /* answer mood tag, e.g. "helpful" */
    uint32_t request_id;
    uint32_t revision; /* bumped on each snapshot change */
} assistant_service_snapshot_t;

/* Start the background task. Safe to call once. */
esp_err_t assistant_service_start(void);

/*
 * Submit a query (contract `type` + query text). Returns ESP_ERR_INVALID_STATE
 * if a request is already in flight (single request per device).
 */
esp_err_t assistant_service_submit(const char *type, const char *text);

void assistant_service_get_snapshot(assistant_service_snapshot_t *snapshot);
const char *assistant_service_status_label(assistant_service_status_t status);

#endif
