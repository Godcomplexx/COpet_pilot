#ifndef COPET_SETTINGS_UI_H
#define COPET_SETTINGS_UI_H

#include <stdint.h>

#include "core/copet_behavior.h"
#include "modes/desk_mode.h"
#include "modes/settings_mode.h"
#include "services/weather_service.h"

void settings_ui_render(uint8_t *framebuffer, int width, int height,
                        const desk_mode_view_t *view,
                        const settings_mode_t *settings,
                        const copet_behavior_view_t *behavior,
                        const char *network_status,
                        const weather_service_snapshot_t *weather);

#endif
