#include "core/input.h"

namespace {
button_state held_only(const button_state& button) {
    return button_state{button.is_down, false, false};
}
}

input_state next_frame_input(const input_state& input) {
    input_state next;
    next.mouse_x = input.mouse_x;
    next.mouse_y = input.mouse_y;
    next.up = held_only(input.up);
    next.down = held_only(input.down);
    next.left = held_only(input.left);
    next.right = held_only(input.right);
    next.space = held_only(input.space);
    next.left_shift = held_only(input.left_shift);
    next.right_shift = held_only(input.right_shift);
    next.ctrl = held_only(input.ctrl);
    next.enter = held_only(input.enter);
    next.tab = held_only(input.tab);
    next.caps_lock = held_only(input.caps_lock);
    next.backspace = held_only(input.backspace);
    next.escape = held_only(input.escape);
    next.key_w = held_only(input.key_w);
    next.key_a = held_only(input.key_a);
    next.key_s = held_only(input.key_s);
    next.key_d = held_only(input.key_d);
    next.key_e = held_only(input.key_e);
    next.key_f = held_only(input.key_f);
    next.key_q = held_only(input.key_q);
    next.key_m = held_only(input.key_m);
    next.key_1 = held_only(input.key_1);
    next.key_2 = held_only(input.key_2);
    next.key_g = held_only(input.key_g);
    next.key_t = held_only(input.key_t);
    next.mouse_left = held_only(input.mouse_left);
    return next;
}
