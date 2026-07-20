#include "modes/animation_mode.h"

void animation_mode_start(animation_mode_t *animation, int64_t now_us)
{
    animation->frame = 0;
    animation->next_frame_us = now_us + ANIMATION_FRAME_INTERVAL_US;
}

bool animation_mode_tick(animation_mode_t *animation, int64_t now_us)
{
    if (now_us < animation->next_frame_us) {
        return false;
    }
    animation->frame = (animation->frame + 1) % ANIMATION_FRAME_COUNT;
    animation->next_frame_us = now_us + ANIMATION_FRAME_INTERVAL_US;
    return true;
}
