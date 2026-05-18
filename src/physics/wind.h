#pragma once

#include <cstdint>

#include <glm/vec3.hpp>

#include "physics/physics_tuning.h"

struct wind_state {
    glm::vec3 velocity{0.0f, 0.0f, 0.0f};
};

wind_state sample_wind(std::uint32_t seed, float time, const wind_tuning& tuning);
