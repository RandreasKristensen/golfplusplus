#include "core/key_bindings.h"

#include <algorithm>

game_input game_input_from_keys(const input_state& keys, const float mouse_look_radians, const setup_tuning& setup) {
    game_input input;
    input.forward_held = keys.key_w.is_down;
    input.run_held = keys.left_shift.is_down || keys.right_shift.is_down;
    input.back_held = keys.key_s.is_down;
    input.strafe_left_held = keys.key_a.is_down;
    input.strafe_right_held = keys.key_d.is_down;
    input.turn_left_held = keys.left.is_down;
    input.turn_right_held = keys.right.is_down;
    input.look_up_held = keys.up.is_down;
    input.look_down_held = keys.down.is_down;
    // Mouse right and down are positive counts; the look turns left and up.
    const float mouse_yaw = -static_cast<float>(keys.mouse_dx) * mouse_look_radians;
    const float mouse_pitch = -static_cast<float>(keys.mouse_dy) * mouse_look_radians;
    // The left button held pulls the setup; the right button held past a
    // small movement turns the stance or swipes. Neither looks. Let go of
    // the right before that, it was a click: aim at the view.
    const bool dragged = keys.right_drag_counts > setup.drag_threshold_counts;
    if (keys.mouse_left.is_down) {
        input.pull_x = static_cast<float>(keys.mouse_dx);
        input.pull_y = static_cast<float>(keys.mouse_dy);
    } else if (keys.mouse_right.is_down && dragged) {
        input.stance_drag_yaw = mouse_yaw;
        input.swipe = static_cast<float>(keys.mouse_dx) / static_cast<float>(std::max(1, setup.swipe_drag_counts));
    } else {
        input.look_yaw = mouse_yaw;
        input.look_pitch = mouse_pitch;
    }
    // Two fingers on a touchpad move the page: across to the right they
    // scroll left, and swipe right.
    if (setup.swipe_wheel_amount > 0.0f) {
        input.swipe -= keys.wheel_x / setup.swipe_wheel_amount;
    }
    input.aim_at_view = (keys.mouse_right.released && !dragged) || keys.key_t.pressed;

    // The wheel away from me, like W and up, is more: a longer club in the
    // picker, and at address a longer backswing.
    const bool shift = keys.left_shift.is_down || keys.right_shift.is_down;
    input.backswing_steps = keys.wheel_steps;
    input.picker_steps = keys.wheel_steps + static_cast<int>(keys.key_w.pressed) - static_cast<int>(keys.key_s.pressed) +
        static_cast<int>(keys.up.pressed) - static_cast<int>(keys.down.pressed);
    input.take_club = keys.mouse_left.pressed;
    input.move_bag = keys.key_b.pressed;
    input.reset_setup = keys.backspace.pressed || keys.mouse_middle.pressed;
    input.snap_view = keys.key_r.pressed;

    input.interact = keys.key_f.pressed;
    input.swing = keys.space.pressed;
    input.jump = keys.space.pressed;
    input.cancel = keys.escape.pressed;
    input.use_smokable = keys.key_1.pressed;
    input.use_drinkable = keys.key_2.pressed;
    input.toggle_flag = keys.key_p.pressed;
    input.rangefinder_held = keys.ctrl.is_down;
    input.course_map_held = keys.key_m.is_down;
    input.scorecard_held = keys.tab.is_down;
    input.skills_panel_held = keys.key_k.is_down;
    input.toggle_pacing = keys.caps_lock.pressed;
    input.crouch_held = keys.key_c.is_down;
    input.group = keys.key_g.pressed && !shift;
    input.leave_group = keys.key_g.pressed && shift;
    return input;
}
