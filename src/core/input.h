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
    // How far the mouse moved this frame, in its counts (right and down
    // positive); captured while a round plays (set_mouse_captured).
    int mouse_dx = 0;
    int mouse_dy = 0;
    // Notches the wheel turned this frame: positive away from the player.
    int wheel_steps = 0;
    // How far the mouse has moved (its counts, either way) since the right
    // button went down: a click or a drag (core/key_bindings.h).
    int right_drag_counts = 0;
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
    button_state alt;
    button_state enter;
    button_state tab;
    button_state caps_lock;
    button_state backspace;
    button_state escape;
    button_state key_w;
    button_state key_a;
    button_state key_b;
    button_state key_s;
    button_state key_d;
    button_state key_e;
    button_state key_f;
    button_state key_q;
    button_state key_r;
    button_state key_v;
    button_state key_z;
    button_state key_c;
    button_state key_m;
    button_state key_1;
    button_state key_2;
    button_state key_g;
    button_state key_p;
    button_state key_t;  // also, with the profiler shown, scrubs the sky (core/sky_frame.h)
    button_state key_f1;  // developer: the god menu (core/god_menu.h)
    button_state mouse_left;
    button_state mouse_right;
    button_state mouse_middle;
};

// Clears the per-frame flags (pressed, released, typed text, quit, mouse movement and wheel) and keeps
// which buttons are held, and how far a held right button has dragged.
input_state next_frame_input(const input_state& input);
