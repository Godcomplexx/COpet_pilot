#ifndef COPET_CORE_MODES_H
#define COPET_CORE_MODES_H

/*
 * Shared top-level state machine identifiers.
 *
 * The value each mode carries is decided by app_main, but the enum lives here
 * so logic modules (menu, focus, ...) can refer to modes without depending on
 * app_main. This keeps every mode module host-testable on its own.
 */
typedef enum {
    COPET_MODE_BOOT,
    COPET_MODE_MENU,
    COPET_MODE_DESK,
    COPET_MODE_FOCUS,
    COPET_MODE_PHONE_BRIDGE,
    COPET_MODE_MINI_TV,
    COPET_MODE_OUTDOOR,
    COPET_MODE_SETTINGS,
    COPET_MODE_SLEEP,
    COPET_MODE_ANIMATION,
} copet_mode_t;

#endif
