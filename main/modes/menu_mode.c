#include "modes/menu_mode.h"

/*
 * Only shipped, working screens appear here. Hidden features (ASSISTANT,
 * MINI TV) and diagnostic-only paths (PHONE BRIDGE, AUDIO LOOPBACK) stay out
 * of the product menu until they are ready.
 */
static const menu_item_t MENU_ITEMS[] = {
    {"FOCUS", "25/5 TIMER", COPET_MODE_FOCUS},
    {"ANIMATION", "LOCAL GALLERY", COPET_MODE_ANIMATION},
    {"SETTINGS", "DEVICE STATUS", COPET_MODE_SETTINGS},
};

void menu_mode_init(menu_mode_t *menu)
{
    menu->selected = 0;
}

size_t menu_mode_count(void)
{
    return sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);
}

const menu_item_t *menu_mode_item(size_t index)
{
    if (index >= menu_mode_count()) {
        return NULL;
    }
    return &MENU_ITEMS[index];
}

const menu_item_t *menu_mode_selected(const menu_mode_t *menu)
{
    return &MENU_ITEMS[menu->selected];
}

void menu_mode_scroll(menu_mode_t *menu, int32_t logical_steps)
{
    const int32_t item_count = (int32_t)menu_mode_count();
    const int32_t wrapped =
        ((int32_t)menu->selected + logical_steps) % item_count;
    menu->selected = (size_t)(wrapped < 0 ? wrapped + item_count : wrapped);
}
