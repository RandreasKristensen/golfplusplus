#pragma once

// Physical keyboard and mouse state for one frame, filled by event_loop.
// Gameplay never reads this directly: key_bindings turns it into game_input.

#include <string>

struct button_state {
    bool is_down = false;
    bool pressed = false;   // went down this frame
    bool released = false;  // went up this frame
};

struct input_state {
    bool quit_requested = false;
    int mouse_x = 0;
    int mouse_y = 0;
    // UTF-8 text typed this frame (SDL_TEXTINPUT). Only filled while a text
    // field has enabled text input, see set_text_input_enabled.
    std::string text_typed;
    button_state up;
    button_state down;
    button_state left;
    button_state right;
    button_state space;
    button_state left_shift;
    button_state right_shift;
    button_state ctrl;
    button_state enter;
    button_state tab;
    button_state caps_lock;
    button_state backspace;
    button_state escape;
    button_state key_r;
    button_state key_1;
    button_state key_2;
    button_state mouse_left;
};

// Clears the per-frame flags (pressed, released, typed text, quit) and keeps
// which buttons are held.
input_state next_frame_input(const input_state& input);
