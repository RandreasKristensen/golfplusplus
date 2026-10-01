#pragma once

// Deterministic wind for ball flight.

#include "physics/physics_tuning.h"

#include <cstdint>

#include <glm/vec3.hpp>

struct wind_state {
    glm::vec3 velocity{0.0f};
};

// Wind is a pure function of (seed, time): each hole's wind_seed gives it a
// stable character, and the same time always gives the same wind.
wind_state sample_wind(std::uint32_t seed, float time, const wind_tuning& tuning);
