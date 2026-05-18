#pragma once

#include "physics/ball_state.h"
#include "physics/wind.h"

ball_state step(const ball_state in, const wind_state wind, float dt);
