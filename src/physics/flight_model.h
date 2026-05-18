#pragma once

#include <glm/vec3.hpp>

glm::vec3 gravity_accel();
glm::vec3 drag_accel(const glm::vec3& velocity, float drag_coeff, float dt);
glm::vec3 magnus_accel(const glm::vec3& spin, const glm::vec3& velocity, float magnus_coeff);
