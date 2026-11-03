#pragma once

#include "physics/ball_state.h"
#include "physics/terrain.h"

// Distance from the terrain surface to the ball centre, along the surface
// normal. Equal to the ball radius when the ball rests on the ground.
float ball_support_distance(const ball_state& ball, const terrain_sample& terrain);

// True when the ball touches the ground (within a millimetre).
bool ball_is_grounded(const ball_state& ball, const terrain_sample& terrain);

// True over water when the ball's lowest point is below the pond's surface
// (terrain.water_level; the sampled point is the bed of its bowl).
bool ball_in_water(const ball_state& ball, const terrain_sample& terrain);
