#include "physics/flight_model.h"

#include <glm/geometric.hpp>

glm::vec3 gravity_accel() {
    return glm::vec3(0.0f, -9.81f, 0.0f);
}

glm::vec3 drag_accel(const glm::vec3& velocity, const float drag_coeff) {
    const float speed = glm::length(velocity);
    if (speed <= 0.0f) {
        return glm::vec3(0.0f, 0.0f, 0.0f);
    }

    return -drag_coeff * speed * velocity;
}

glm::vec3 magnus_accel(const glm::vec3& spin, const glm::vec3& velocity, const float magnus_coeff) {
    return magnus_coeff * glm::cross(spin, velocity);
}
