#pragma once

// What the player wants this frame, independent of keys. core/key_bindings
// fills it from the keyboard and mouse; tests fill it directly.
// "held" fields are true every frame the control is down; the others only on
// the frame it is pressed.
struct game_input {
    // Walking relative to where I look; at the wheel, throttle and reverse;
    // at address, a longer or shorter cap on the backswing.
    bool forward_held = false;
    bool run_held = false;
    bool back_held = false;
    // Stepping sideways on foot; at the wheel, steering; at address,
    // turning the stance (the aim), looking up or down.
    bool strafe_left_held = false;
    bool strafe_right_held = false;
    // Turning where I look at a steady rate, and looking up or down; at
    // address looking down at the ball, up and down open and close the
    // face, left and right move the ball in the stance that way on screen.
    bool turn_left_held = false;
    bool turn_right_held = false;
    bool look_up_held = false;
    bool look_down_held = false;
    // How far the mouse turned the look this frame, radians: positive left
    // (towards +X, like yaw) and up.
    float look_yaw = 0.0f;
    float look_pitch = 0.0f;
    // The club picker at my bag (game/golf_bag.h): up (positive, a longer
    // club) or down the list this frame, by W and S, the arrows or a notch
    // of the wheel each; take the highlighted club by a left click (or F).
    int picker_steps = 0;
    bool take_club = false;
    // B: my bag off my cart onto my back, down at my feet, back up from the
    // ground, or onto my cart.
    bool move_bag = false;
    // With what is in reach: tee up, step up to my ball, pick up the one in
    // the cup, claim a collectible, step into or out of a cart; at address,
    // step away from the ball.
    bool interact = false;
    // At address, steps of the backswing's cap this frame by the wheel:
    // positive longer.
    int backswing_steps = 0;
    // A left-drag this frame, mouse counts, right and down positive: at
    // address looking down, pulling the face (up and down) and the ball
    // (across) as the arrows do.
    float pull_x = 0.0f;
    float pull_y = 0.0f;
    // At address looking up, turn the stance so the path points at the
    // ground under the view's centre (a right-click, or T).
    bool aim_at_view = false;
    // At address looking up, a fine turn of the stance by a right-drag,
    // radians, positive left.
    float stance_drag_yaw = 0.0f;
    // How far a swipe went across this frame, in whole swipes, right
    // positive: a right-drag, or a touchpad's two fingers (game/swing.h).
    float swipe = 0.0f;
    bool reset_setup = false;  // back to the club's stock setup (Backspace, middle-click)
    bool snap_view = false;    // turn my head between looking down and up (R)
    // At address looking down, Space: the swipe right, and once the club is
    // going back the swipe left.
    bool swing = false;
    bool jump = false;  // on foot
    bool cancel = false;  // step away from the ball
    // Use what I smoke, or what I drink (game/loadout.h).
    bool use_smokable = false;
    bool use_drinkable = false;
    // Take the flag out of the cup in reach, or put it back.
    bool toggle_flag = false;
    bool rangefinder_held = false;
    bool course_map_held = false;
    bool scorecard_held = false;
    bool skills_panel_held = false;
    // Start pacing a distance off on foot, or stop (game/pacing.h).
    bool toggle_pacing = false;
    // Crouch on foot to read the green (game/green_read.h).
    bool crouch_held = false;
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
