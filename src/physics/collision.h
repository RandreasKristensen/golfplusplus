#pragma once

#include "physics/ball_state.h"
#include "physics/terrain.h"

ball_state resolve_ground_collision(const ball_state in, float ground_y, float restitution, float friction);
ball_state resolve_terrain_collision(const ball_state in,
                                     const terrain_sample& terrain,
                                     float restitution,
                                     float friction);
