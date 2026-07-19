#ifndef COPET_SETTINGS_MODE_H
#define COPET_SETTINGS_MODE_H

#include <stdbool.h>

typedef struct {
    bool sound_enabled;
} settings_mode_t;

void settings_mode_init(settings_mode_t *settings, bool sound_enabled);
bool settings_mode_toggle_sound(settings_mode_t *settings);
const char *settings_mode_sound_label(const settings_mode_t *settings);

#endif
