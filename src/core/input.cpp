#include "core/input.h"

#include <cmath>

namespace {
button_state held_only(const button_state& button) {
    return button_state{button.is_down, false, false};
}
}

int wheel_steps_away(const int y, const float precise_x, const float precise_y, const bool flipped) {
    const auto fraction = [](const float amount) { return std::abs(amount - std::round(amount)) > 0.001f; };
    const bool touchpad = fraction(precise_x) || fraction(precise_y);
    return (flipped != touchpad) ? -y : y;
}

input_state next_frame_input(const input_state& input) {
    input_state next;
    next.mouse_x = input.mouse_x;
    next.mouse_y = input.mouse_y;
    next.right_drag_counts = input.right_drag_counts;
    next.up = held_only(input.up);
    next.down = held_only(input.down);
    next.left = held_only(input.left);
    next.right = held_only(input.right);
    next.space = held_only(input.space);
    next.left_shift = held_only(input.left_shift);
    next.right_shift = held_only(input.right_shift);
    next.ctrl = held_only(input.ctrl);
    next.alt = held_only(input.alt);
    next.enter = held_only(input.enter);
    next.tab = held_only(input.tab);
    next.caps_lock = held_only(input.caps_lock);
    next.backspace = held_only(input.backspace);
    next.escape = held_only(input.escape);
    next.key_w = held_only(input.key_w);
    next.key_a = held_only(input.key_a);
    next.key_s = held_only(input.key_s);
    next.key_d = held_only(input.key_d);
    next.key_f = held_only(input.key_f);
    next.key_r = held_only(input.key_r);
    next.key_b = held_only(input.key_b);
    next.key_c = held_only(input.key_c);
    next.key_m = held_only(input.key_m);
    next.key_1 = held_only(input.key_1);
    next.key_2 = held_only(input.key_2);
    next.key_g = held_only(input.key_g);
    next.key_k = held_only(input.key_k);
    next.key_p = held_only(input.key_p);
    next.key_t = held_only(input.key_t);
    next.key_f2 = held_only(input.key_f2);
    next.mouse_left = held_only(input.mouse_left);
    next.mouse_right = held_only(input.mouse_right);
    next.mouse_middle = held_only(input.mouse_middle);
    return next;
}
