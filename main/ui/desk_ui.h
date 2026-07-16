#ifndef COPET_DESK_UI_H
#define COPET_DESK_UI_H

#include <stdint.h>

#include "modes/desk_mode.h"

void desk_ui_render(uint8_t *framebuffer, int width, int height,
                    const desk_mode_view_t *view);

#endif
