#pragma once

// What the player wants this frame, independent of keys. core/key_bindings
// fills it from the keyboard; tests (and later the network) fill it directly.
// "held" fields are true every frame the control is down; the others only on
// the frame it is pressed.
struct game_input {
    bool forward_held = false;
    bool back_held = false;
    bool turn_left_held = false;
    bool turn_right_held = false;
    bool previous_club = false;
    bool next_club = false;
    // Interact, start the swing, set power, or drift (in the cart).
    bool action = false;
    bool cancel = false;  // leave shot setup
    bool retee = false;
    bool smoke = false;
    bool drink = false;
    bool cart_held = false;
    bool rangefinder_held = false;
    bool course_map_held = false;
    bool scorecard_held = false;
    bool skills_panel_held = false;
    bool group = false;        // join the nearest player's group, or start one
    bool leave_group = false;
};
