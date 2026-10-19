#include "core/key_bindings.h"

game_input game_input_from_keys(const input_state& keys, const float mouse_look_radians) {
    game_input input;
    input.forward_held = keys.key_w.is_down;
    input.back_held = keys.key_s.is_down;
    input.strafe_left_held = keys.key_a.is_down;
    input.strafe_right_held = keys.key_d.is_down;
    input.turn_left_held = keys.left.is_down;
    input.turn_right_held = keys.right.is_down;
    input.look_up_held = keys.up.is_down;
    input.look_down_held = keys.down.is_down;
    // Mouse right and down are positive counts; the look turns left and up.
    input.look_yaw = -static_cast<float>(keys.mouse_dx) * mouse_look_radians;
    input.look_pitch = -static_cast<float>(keys.mouse_dy) * mouse_look_radians;
    // The wheel away from me, like W and up, is a longer club.
    input.wheel_club_steps = keys.wheel_steps;
    input.club_steps = keys.wheel_steps + static_cast<int>(keys.key_w.pressed || keys.up.pressed) -
        static_cast<int>(keys.key_s.pressed || keys.down.pressed);
    input.interact = keys.key_f.pressed;
    input.click = keys.mouse_left.pressed;
    // E and Q the way the club swings: back, then down and through.
    input.backswing = keys.key_e.pressed;
    input.downswing = keys.key_q.pressed;
    input.jump = keys.space.pressed;
    input.cancel = keys.escape.pressed;
    input.smoke = keys.key_1.pressed;
    input.drink = keys.key_2.pressed;
    input.rangefinder_held = keys.left_shift.is_down;
    input.course_map_held = keys.key_m.is_down;
    input.scorecard_held = keys.tab.is_down;
    input.skills_panel_held = keys.caps_lock.is_down;
    const bool shift = keys.left_shift.is_down || keys.right_shift.is_down;
    input.group = keys.key_g.pressed && !shift;
    input.leave_group = keys.key_g.pressed && shift;
    return input;
}
