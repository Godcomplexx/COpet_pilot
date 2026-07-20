#ifndef COPET_MENU_UI_H
#define COPET_MENU_UI_H

#include <stdint.h>

#include "modes/menu_mode.h"

void menu_ui_render(uint8_t *framebuffer, int width, int height,
                    const menu_mode_t *menu,
                    const char *network_status);

#endif
