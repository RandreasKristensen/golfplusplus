#pragma once

#include "physics/ball_state.h"
#include "physics/wind.h"

// Pure function: no side effects, no global state, deterministic output.
ball_state step(const ball_state in, const wind_state wind, float dt);
