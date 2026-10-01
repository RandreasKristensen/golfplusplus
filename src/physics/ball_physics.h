#pragma once

#include "physics/ball_state.h"
#include "physics/physics_tuning.h"
#include "physics/wind.h"

// Advances a ball in flight by `dt` (gravity, drag, Magnus lift, spin decay).
// Collisions are resolved separately (collision.h, tree_collision.h).
ball_state step_ball_flight(const ball_state& in, const wind_state& wind, float dt, const physics_tuning& tuning);
