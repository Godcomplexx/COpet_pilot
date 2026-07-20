#ifndef COPET_PIP_FACE_PORT_H
#define COPET_PIP_FACE_PORT_H

#include <stdint.h>

#include "core/copet_behavior.h"
#include "modes/desk_mode.h"

void pip_face_port_render(uint8_t *framebuffer, int screen_width,
                          int screen_height, int x, int y,
                          int width, int height,
                          const desk_mode_view_t *view,
                          const copet_behavior_view_t *behavior);

#endif
