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
    int mouse_x = 0;
    int mouse_y = 0;
    button_state left;
    button_state right;
    button_state up;
    button_state down;
    button_state space;
    button_state shift;
    button_state ctrl;
    button_state enter;
    button_state backspace;
    button_state retee;
    button_state escape;
    button_state mouse_left;

    void reset_frame() {
        quit_requested = false;
        left.reset_frame();
        right.reset_frame();
        up.reset_frame();
        down.reset_frame();
        space.reset_frame();
        shift.reset_frame();
        ctrl.reset_frame();
        enter.reset_frame();
        backspace.reset_frame();
        retee.reset_frame();
        escape.reset_frame();
        mouse_left.reset_frame();
    }
};
