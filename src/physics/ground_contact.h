#pragma once

#include "physics/ball_state.h"
#include "physics/physics_tuning.h"
#include "physics/terrain.h"

// Distance from the terrain surface to the ball centre, along the surface
// normal. Equal to the ball radius when the ball rests on the ground.
float ball_support_distance(const ball_state& ball, const terrain_sample& terrain);

// True when the ball touches the ground (within a millimetre).
bool ball_is_grounded(const ball_state& ball, const terrain_sample& terrain);

// True when the ball's lowest point is below the water surface. Water zones
// are carved `water_depth` below the surface, so the surface sits that far
// above the sampled mesh.
bool ball_in_water(const ball_state& ball, const terrain_sample& terrain, float water_depth);

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

// True when the horizontal path start -> end passes within `radius` of the
// cup centre while the ball centre is at most `max_height_above_cup` above it.
bool path_crosses_cup(const glm::vec3& start,
                      const glm::vec3& end,
                      const glm::vec3& cup_center,
                      float radius,
                      float max_height_above_cup);
