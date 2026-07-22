#ifndef COPET_ASSISTANT_UI_H
#define COPET_ASSISTANT_UI_H

#include <stdint.h>

#include "modes/assistant_mode.h"

enum {
    ASSISTANT_UI_LINE_MAX = 30,  /* per wrapped line, including the NUL */
    ASSISTANT_UI_MAX_LINES = 7,
};

/*
 * Greedy word-wrap `text` into `lines` at up to `max_chars` per line; words
 * longer than a line are hard-split. Returns the number of lines produced
 * (<= max_lines). Pure -- host-tested independently of the renderer.
 */
int assistant_ui_wrap(const char *text, int max_chars,
                      char lines[][ASSISTANT_UI_LINE_MAX], int max_lines);

/* Render the Assistant card for the current sub-state. */
void assistant_ui_render(uint8_t *framebuffer, int width, int height,
                         const assistant_mode_t *assistant);

#endif
