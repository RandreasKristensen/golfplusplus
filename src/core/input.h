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
    button_state key_1;
    button_state key_2;
    button_state left;
    button_state left_shift;
    button_state right;
    button_state up;
    button_state down;
    button_state space;
    button_state shift;
    button_state ctrl;
    button_state enter;
    button_state tab;
    button_state caps_lock;
    button_state backspace;
    button_state retee;
    button_state escape;
    button_state mouse_left;

    void reset_frame() {
        quit_requested = false;
        key_1.reset_frame();
        key_2.reset_frame();
        left.reset_frame();
        left_shift.reset_frame();
        right.reset_frame();
        up.reset_frame();
        down.reset_frame();
        space.reset_frame();
        shift.reset_frame();
        ctrl.reset_frame();
        enter.reset_frame();
        tab.reset_frame();
        caps_lock.reset_frame();
        backspace.reset_frame();
        retee.reset_frame();
        escape.reset_frame();
        mouse_left.reset_frame();
    }
};
