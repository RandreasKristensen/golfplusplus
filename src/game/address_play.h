#pragma once

// Standing over my ball (game_mode::addressing): stepping up to it at the
// stock setup of the club in hand, the dials and my head's look while I
// set the shot up, and the swing in the control in use (game/swing.h),
// which strikes the ball at impact.

#include "game/game_input.h"
#include "game/game_state.h"
#include "profiling/profiling.h"

// F at my ball: over it at the club's stock setup, aimed where I look,
// my head turned to the target view. Not with my bag on my back: a notice
// says to set it down.
void step_up_to_ball(game_state& state);

// Over my ball, set up as `last` was: its aim, face, ball position,
// backswing and look, when it was set up with the club in my hand.
void set_up_as(game_state& state, const address_state& last);

void update_addressing(game_state& state, const game_input& input, float dt, frame_profile* profile = nullptr);

// How `setup` would start from my ball: the shot as set up (no dice), in
// still air, from its lie and slope, its first start_line_meters, or to where
// it first comes down if sooner; a putt's
// line putt_line_meters along where it starts (tuning.shot_preview).
shot_preview preview_shot(const game_state& state, const shot_setup& setup);
// Previews my setup again when it is not what the preview shows.
void refresh_shot_preview(game_state& state);

// Where my eyes are and look at address.
eye_view my_address_eye(const game_state& state);
// My shoes on the ground at address.
address_feet my_address_feet(const game_state& state);
