#pragma once

#include "physics/ball_state.h"

ball_state resolve_ground_collision(const ball_state in, float ground_y, float restitution, float friction);
