#ifndef COPET_ANIMATION_MODE_H
#define COPET_ANIMATION_MODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    ANIMATION_FRAME_COUNT = 7,
    ANIMATION_FRAME_INTERVAL_US = 200000,
};

typedef struct {
    size_t frame;
    int64_t next_frame_us;
} animation_mode_t;

/* Rewind to the first frame and schedule the next advance. */
void animation_mode_start(animation_mode_t *animation, int64_t now_us);

/*
 * Advance to the next frame when the interval has elapsed, wrapping back to
 * frame 0 after the last one. Returns true when the frame changed.
 */
bool animation_mode_tick(animation_mode_t *animation, int64_t now_us);

#endif
