#include "core/key_bindings.h"

#include <cstdlib>

game_input game_input_from_keys(const input_state& keys, const float mouse_look_radians, const int drag_threshold_counts) {
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
    const float mouse_yaw = -static_cast<float>(keys.mouse_dx) * mouse_look_radians;
    const float mouse_pitch = -static_cast<float>(keys.mouse_dy) * mouse_look_radians;
    // The right button held past a small movement turns the stance instead
    // of looking; let go before that, it was a click: aim at the view.
    const bool dragged = keys.right_drag_counts > drag_threshold_counts;
    if (keys.mouse_right.is_down && dragged) {
        input.stance_drag_yaw = mouse_yaw;
    } else {
        input.look_yaw = mouse_yaw;
        input.look_pitch = mouse_pitch;
    }
    input.aim_at_view = (keys.mouse_right.released && !dragged) || keys.key_t.pressed;

    // The wheel away from me, like W and up, is more: a longer club in the
    // picker, and at address a longer backswing, the face open, the ball
    // forward or the stance turned left. A modifier picks the dial; without
    // one it is what the view is on.
    const bool shift = keys.left_shift.is_down || keys.right_shift.is_down;
    if (shift) {
        input.aim_steps = keys.wheel_steps;
    } else if (keys.ctrl.is_down) {
        input.face_steps = keys.wheel_steps;
    } else if (keys.alt.is_down) {
        input.ball_steps = keys.wheel_steps;
    } else {
        input.look_target_wheel_steps = keys.wheel_steps;
    }
    input.picker_steps = keys.wheel_steps + static_cast<int>(keys.key_w.pressed) - static_cast<int>(keys.key_s.pressed) +
        static_cast<int>(keys.up.pressed) - static_cast<int>(keys.down.pressed);
    input.take_club = keys.mouse_left.pressed;
    input.move_bag = keys.key_b.pressed;
    input.face_open_held = keys.key_q.is_down;
    input.face_close_held = keys.key_e.is_down;
    input.ball_back_held = keys.key_z.is_down;
    input.ball_forward_held = keys.key_c.is_down;
    input.reset_setup = keys.key_r.pressed || keys.mouse_middle.pressed;
    input.snap_view = keys.key_v.pressed;

    input.interact = keys.key_f.pressed;
    input.swing = keys.space.pressed;
    input.jump = keys.space.pressed;
    input.cancel = keys.escape.pressed;
    input.use_smokable = keys.key_1.pressed;
    input.use_drinkable = keys.key_2.pressed;
    input.toggle_flag = keys.key_p.pressed;
    input.rangefinder_held = keys.left_shift.is_down;
    input.course_map_held = keys.key_m.is_down;
    input.scorecard_held = keys.tab.is_down;
    input.skills_panel_held = keys.caps_lock.is_down;
    input.group = keys.key_g.pressed && !shift;
    input.leave_group = keys.key_g.pressed && shift;
    return input;
}
