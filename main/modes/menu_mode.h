#ifndef COPET_MENU_MODE_H
#define COPET_MENU_MODE_H

#include <stddef.h>
#include <stdint.h>

#include "core/copet_modes.h"

typedef struct {
    const char *label;
    const char *detail;
    copet_mode_t mode;
} menu_item_t;

typedef struct {
    size_t selected;
} menu_mode_t;

/* Start with the first item selected. */
void menu_mode_init(menu_mode_t *menu);

/* Number of visible menu items. */
size_t menu_mode_count(void);

/* Item at index, or NULL when out of range. */
const menu_item_t *menu_mode_item(size_t index);

/* Currently selected item (never NULL for an initialized menu). */
const menu_item_t *menu_mode_selected(const menu_mode_t *menu);

/* Move the selection by logical encoder steps, wrapping both directions. */
void menu_mode_scroll(menu_mode_t *menu, int32_t logical_steps);

#endif
