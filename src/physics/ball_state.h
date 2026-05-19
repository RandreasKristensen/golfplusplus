#pragma once

#include <glm/vec3.hpp>

struct ball_state {
    // Position is the ball center. Radius and mass are part of the physics contract.
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    float radius = 0.021335f;
    float mass = 0.04593f;
    glm::vec3 velocity{0.0f, 0.0f, 0.0f};
    glm::vec3 spin{0.0f, 0.0f, 0.0f};
};
