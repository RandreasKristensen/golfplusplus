#pragma once

#include <glm/vec3.hpp>

struct ball_state {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 velocity{0.0f, 0.0f, 0.0f};
    glm::vec3 spin{0.0f, 0.0f, 0.0f};
};
