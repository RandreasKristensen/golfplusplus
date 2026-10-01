#pragma once

// The forces on a ball in the air; combined by step_ball_flight.

#include <glm/vec3.hpp>

inline constexpr float gravity_meters_per_second2 = 9.81f;

glm::vec3 gravity_acceleration();
// Quadratic drag. Clamped so one step of length `dt` can at most stop the
// ball, never reverse it.
glm::vec3 drag_acceleration(const glm::vec3& velocity, float drag_coeff, float dt);
// Magnus force direction follows the right-hand rule: spin x velocity.
glm::vec3 magnus_acceleration(const glm::vec3& spin, const glm::vec3& velocity, float magnus_coeff);
