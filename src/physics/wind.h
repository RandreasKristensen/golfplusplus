#pragma once

// The wind a ball meets: the air's speed at its height over the ground.

#include "physics/physics_tuning.h"

#include <glm/vec3.hpp>

struct wind_state {
    glm::vec3 velocity{0.0f};
};

// The wind at `height` over the ground, from `reference_wind`, the wind at
// the height wind is measured at (the profile's scale 1). The profile
// follows the log law over grass, so a rolling ball barely feels it and a
// high drive feels it all. Linear between points; held past the last one,
// none at or below the first. Plain arithmetic, so the server's replay
// matches.
wind_state wind_at_height(const glm::vec3& reference_wind, float height, const wind_profile& profile);
