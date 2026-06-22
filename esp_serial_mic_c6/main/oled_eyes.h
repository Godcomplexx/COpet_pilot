#pragma once

typedef enum {
    FACE_IDLE = 0,      // calm idle eyes
    FACE_LISTENING,     // recording
    FACE_PROCESSING,    // phone is recognizing/thinking
    FACE_SPEAKING,      // assistant is speaking
    FACE_TILT_LEFT,     // accelerometer tilt left
    FACE_TILT_RIGHT,    // accelerometer tilt right
    FACE_SHAKE,         // accelerometer shake
    FACE_FLAT,          // board is lying flat / sleepy
    FACE_LOOK_UP,       // accelerometer tilt up
    FACE_LOOK_DOWN,     // accelerometer tilt down
    FACE_HAPPY          // soft happy micro-expression
} face_state_t;

void oled_eyes_start(void);
void oled_eyes_set_state(face_state_t state);
