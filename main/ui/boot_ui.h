#ifndef COPET_BOOT_UI_H
#define COPET_BOOT_UI_H

#include <stdint.h>

void boot_ui_render(uint8_t *framebuffer, int width, int height,
                    uint8_t progress_percent, const char *status);

#endif
