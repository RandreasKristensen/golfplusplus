#include "physics/flight_model.h"

#include <algorithm>

#include <glm/geometric.hpp>

glm::vec3 gravity_acceleration() {
    return glm::vec3(0.0f, -gravity_meters_per_second2, 0.0f);
}

glm::vec3 drag_acceleration(const glm::vec3& velocity, const float drag_coeff, const float dt) {
    const float speed = glm::length(velocity);
    if (speed <= 0.0f || dt <= 0.0f) {
        return glm::vec3(0.0f);
    }

    const float deceleration = std::min(drag_coeff * speed * speed, speed / dt);
    return -(deceleration / speed) * velocity;
}

glm::vec3 magnus_acceleration(const glm::vec3& spin, const glm::vec3& velocity, const float magnus_coeff) {
    return magnus_coeff * glm::cross(spin, velocity);
}
