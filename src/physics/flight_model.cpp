#include "physics/flight_model.h"

#include <algorithm>

#include <glm/geometric.hpp>

glm::vec3 gravity_accel() {
    return glm::vec3(0.0f, -9.81f, 0.0f);
}

glm::vec3 drag_accel(const glm::vec3& velocity, const float drag_coeff, const float dt) {
    const float speed = glm::length(velocity);
    if (speed <= 0.0f || dt <= 0.0f) {
        return glm::vec3(0.0f, 0.0f, 0.0f);
    }

    const float decel = drag_coeff * speed * speed;
    const float max_decel = speed / dt;
    const float clamped = std::min(decel, max_decel);
    return -(clamped / speed) * velocity;
}

glm::vec3 magnus_accel(const glm::vec3& spin, const glm::vec3& velocity, const float magnus_coeff) {
    // Magnus direction follows right-hand rule: spin x velocity.
    return magnus_coeff * glm::cross(spin, velocity);
}
