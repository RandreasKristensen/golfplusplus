#pragma once

// Standing over my ball (game_mode::addressing): stepping up to it at the
// stock setup of the club in hand, the dials and my head's look while I
// set the shot up, and the swing Space commits, which strikes the ball at
// impact.

#include "game/game_input.h"
#include "game/game_state.h"
#include "game/setup_pointer.h"
#include "profiling/profiling.h"

// F at my ball: over it at the club's stock setup, aimed where I look,
// my head turned to the target view. Not with my bag on my back: a notice
// says to set it down.
void step_up_to_ball(game_state& state);

void update_addressing(game_state& state, const game_input& input, float dt, frame_profile* profile = nullptr);

// What `setup` would do from my ball: the shot as set up (no dice), in
// still air, from its lie and slope, to where it first lands and rests; a
// putt only the start line, putt_line_meters along where it starts
// (tuning.shot_preview).
shot_preview preview_shot(const game_state& state, const shot_setup& setup);
// Previews my setup again when it is not what the preview shows.
void refresh_shot_preview(game_state& state);

// Where my eyes are and look at address.
eye_view my_address_eye(const game_state& state);
// The setup on the ground at address; needs a club in hand.
address_marks my_address_marks(const game_state& state);
// What the plain wheel changes now (setup_pointer.h).
setup_part my_pointed_part(const game_state& state);
