#pragma once

// The golf ball between physics steps.

#include <glm/vec3.hpp>

struct ball_state {
    glm::vec3 position{0.0f};  // ball centre
    float radius = 0.0f;       // set from game_tuning::scale on every tee-up
    glm::vec3 velocity{0.0f};
    glm::vec3 spin{0.0f};      // angular velocity, rad/s
};
