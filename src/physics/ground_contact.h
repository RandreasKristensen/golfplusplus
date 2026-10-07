#pragma once

#include "physics/ball_state.h"
#include "physics/physics_tuning.h"
#include "physics/terrain.h"

// Distance from the terrain surface to the ball centre, along the surface
// normal. Equal to the ball radius when the ball rests on the ground.
float ball_support_distance(const ball_state& ball, const terrain_sample& terrain);

// True when the ball touches the ground (within a millimetre).
bool ball_is_grounded(const ball_state& ball, const terrain_sample& terrain);

// True over water when the ball's lowest point is below the pond's surface
// (terrain.water_level; the sampled point is the bed of its bowl).
bool ball_in_water(const ball_state& ball, const terrain_sample& terrain);

// Physics with the extra water drag and spin decay added.
physics_tuning with_water_drag(const physics_tuning& tuning);

// Slows a grounded, rolling ball along the surface by `deceleration * dt`
// (m/s). Leaves airborne or bouncing balls (normal speed above
// `settle_speed`) and balls in water unchanged.
ball_state apply_rolling_friction(const ball_state& ball,
                                  const terrain_sample& terrain,
                                  float deceleration,
                                  float settle_speed,
                                  float dt);

// How close the horizontal path start -> end comes to the cup centre,
// anywhere along it, not only at its ends.
float path_cup_offset(const glm::vec3& start, const glm::vec3& end, const glm::vec3& cup_center);
