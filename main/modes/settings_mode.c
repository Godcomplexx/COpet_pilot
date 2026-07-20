#include "modes/settings_mode.h"

#include <stddef.h>

void settings_mode_init(settings_mode_t *settings, bool sound_enabled)
{
    if (settings == NULL) {
        return;
    }
    settings->sound_enabled = sound_enabled;
}

bool settings_mode_toggle_sound(settings_mode_t *settings)
{
    if (settings == NULL) {
        return false;
    }
    settings->sound_enabled = !settings->sound_enabled;
    return settings->sound_enabled;
}

const char *settings_mode_sound_label(const settings_mode_t *settings)
{
    return settings != NULL && settings->sound_enabled ? "ON" : "OFF";
}
