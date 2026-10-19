#pragma once

// What the player wants this frame, independent of keys. core/key_bindings
// fills it from the keyboard and mouse; tests fill it directly.
// "held" fields are true every frame the control is down; the others only on
// the frame it is pressed.
struct game_input {
    // Walking relative to where I look; at the wheel, throttle and reverse.
    bool forward_held = false;
    bool back_held = false;
    // Stepping sideways on foot; at the wheel, steering; aiming, turning the aim.
    bool strafe_left_held = false;
    bool strafe_right_held = false;
    // Turning where I look (and aim) at a steady rate, and looking up or down.
    bool turn_left_held = false;
    bool turn_right_held = false;
    bool look_up_held = false;
    bool look_down_held = false;
    // How far the mouse turned the look this frame, radians: positive left
    // (towards +X, like yaw) and up.
    float look_yaw = 0.0f;
    float look_pitch = 0.0f;
    // Clubs up (positive) or down the bag this frame (clubs are in bag order,
    // shortest first): a key press or a notch of the wheel is one club.
    int club_steps = 0;
    // With what is in reach: start a hole, take up my ball, pick up the one in
    // the cup, claim a collectible, step into or out of a cart; aiming, step
    // up to the ball.
    bool interact = false;
    // Over the ball: start the backswing (the power meter), then time the
    // downswing, which hits at the meter's power.
    bool backswing = false;
    bool downswing = false;
    bool jump = false;  // on foot
    bool cancel = false;  // leave shot setup
    bool retee = false;
    bool smoke = false;
    bool drink = false;
    bool rangefinder_held = false;
    bool course_map_held = false;
    bool scorecard_held = false;
    bool skills_panel_held = false;
    bool group = false;        // join the nearest player's group, or start one
    bool leave_group = false;
};

// +1 turning left (towards +X), -1 right, 0 for neither or both.
inline float turn_direction(const game_input& input) {
    return static_cast<float>(input.turn_left_held) - static_cast<float>(input.turn_right_held);
}

// +1 stepping (or steering) left, -1 right, 0 for neither or both.
inline float strafe_direction(const game_input& input) {
    return static_cast<float>(input.strafe_left_held) - static_cast<float>(input.strafe_right_held);
}

// +1 forward, -1 back, 0 for neither or both.
inline float walk_direction(const game_input& input) {
    return static_cast<float>(input.forward_held) - static_cast<float>(input.back_held);
}

// +1 looking up, -1 down, 0 for neither or both.
inline float look_pitch_direction(const game_input& input) {
    return static_cast<float>(input.look_up_held) - static_cast<float>(input.look_down_held);
}
