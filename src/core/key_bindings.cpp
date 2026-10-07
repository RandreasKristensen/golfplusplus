#include "core/key_bindings.h"

game_input game_input_from_keys(const input_state& keys) {
    game_input input;
    input.forward_held = keys.up.is_down;
    input.back_held = keys.down.is_down;
    input.turn_left_held = keys.left.is_down;
    input.turn_right_held = keys.right.is_down;
    input.longer_club = keys.up.pressed;
    input.shorter_club = keys.down.pressed;
    input.action = keys.space.pressed;
    input.cancel = keys.backspace.pressed || keys.escape.pressed;
    input.retee = keys.key_r.pressed;
    input.smoke = keys.key_1.pressed;
    input.drink = keys.key_2.pressed;
    input.cart_held = keys.left_shift.is_down;
    input.rangefinder_held = keys.right_shift.is_down;
    input.course_map_held = keys.enter.is_down;
    input.scorecard_held = keys.tab.is_down;
    input.skills_panel_held = keys.caps_lock.is_down;
    const bool shift = keys.left_shift.is_down || keys.right_shift.is_down;
    input.group = keys.key_g.pressed && !shift;
    input.leave_group = keys.key_g.pressed && shift;
    return input;
}
