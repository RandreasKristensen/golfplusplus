#pragma once

#include "physics/ball_state.h"
#include "physics/flight_model.h"
#include "physics/physics_tuning.h"
#include "physics/wind.h"

// Advances a ball in flight by `dt`: gravity, drag and lift in `air` moving
// with `wind`, its spin decaying; under water also the water's drag and
// spin loss (physics_tuning). Collisions are resolved separately
// (collision.h, solid.h).
ball_state step_ball_flight(const ball_state& in, const wind_state& wind, const air_state& air, bool under_water, float dt,
                            const physics_tuning& tuning);
