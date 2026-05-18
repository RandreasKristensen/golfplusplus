#pragma once

struct button_state {
    bool is_down = false;
    bool pressed = false;
    bool released = false;

    void reset_frame() {
        pressed = false;
        released = false;
    }
};

struct input_state {
    bool quit_requested = false;
    button_state left;
    button_state right;
    button_state up;
    button_state down;
    button_state space;
    button_state escape;

    void reset_frame() {
        quit_requested = false;
        left.reset_frame();
        right.reset_frame();
        up.reset_frame();
        down.reset_frame();
        space.reset_frame();
        escape.reset_frame();
    }
};
