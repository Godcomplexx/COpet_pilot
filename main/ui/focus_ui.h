#ifndef COPET_FOCUS_UI_H
#define COPET_FOCUS_UI_H

#include <stdint.h>

#include "core/copet_behavior.h"
#include "modes/focus_mode.h"

void focus_ui_render(uint8_t *framebuffer, int width, int height,
                     const focus_mode_t *focus,
                     const copet_behavior_view_t *behavior);

#endif
