#pragma once

// Me walking on foot, in fixed steps whatever the frame's length
// (game/movement_step.h): the same steps the server plays online, where
// each step's input is recorded for it (game/motion_sync.h). A hop pressed
// goes into the next step.

#include "game/game_input.h"
#include "profiling/profiling.h"

struct game_state;

// This frame's steps on foot, and online a hop rising past airborne_height
// tells others I hopped.
void walk_steps(game_state& state, const game_input& input, float dt, frame_profile* profile);

// Not walking this frame (sat in a cart, handling my bag or a club): no
// time saved up for steps, and no step's speed carried over.
void stop_walking(game_state& state);
