#ifndef COPET_DESK_UI_H
#define COPET_DESK_UI_H

#include <stdint.h>

#include "core/copet_behavior.h"
#include "modes/desk_mode.h"
#include "services/weather_service.h"

typedef enum {
    DESK_UI_ENVIRONMENT_ROOM,
    DESK_UI_ENVIRONMENT_OUTDOOR,
} desk_ui_environment_t;

void desk_ui_render(uint8_t *framebuffer, int width, int height,
                    const desk_mode_view_t *view,
                    const copet_behavior_view_t *behavior,
                    const char *network_status,
                    const weather_service_snapshot_t *weather,
                    desk_ui_environment_t environment);

#endif
