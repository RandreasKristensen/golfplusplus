#pragma once

#include "physics/ball_state.h"
#include "physics/terrain.h"

// Contact friction is the fraction of tangential speed lost per
// `contact_friction_reference_seconds` of contact, so the result does not
// depend on the frame rate.
inline constexpr float contact_friction_reference_seconds = 1.0f / 60.0f;

// Fraction of tangential speed kept after `dt` seconds of contact.
float contact_friction_keep(float friction, float dt);

// Pushes the ball out of the terrain along the surface normal, reflects the
// normal velocity by `restitution` and applies contact friction.
ball_state resolve_terrain_collision(const ball_state& in,
                                     const terrain_sample& terrain,
                                     float restitution,
                                     float friction,
                                     float dt);
