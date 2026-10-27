#pragma once

// The forces on a ball in the air, from its spin and the air it flies
// through; combined by step_ball_flight. Real units: m/s, rad/s, kg/m^3.

#include "physics/physics_tuning.h"

#include <glm/vec3.hpp>

inline constexpr float gravity_meters_per_second2 = 9.81f;

// The air a shot flies through: how dense and how sticky.
struct air_state {
    float density = 0.0f;    // kg/m^3
    float viscosity = 0.0f;  // Pa s
};

// The air `elevation` metres above sea level at `temperature_c`: the
// standard atmosphere's pressure there (the barometric formula), and the
// density of air that warm at that pressure.
air_state air_at(const air_tuning& tuning, float elevation, float temperature_c);

struct aero_coefficients {
    float lift = 0.0f;
    float drag = 0.0f;
};
// A ball of `radius` moving at `speed` through `air`, spinning at `spin`
// rad/s (ball_aero's fit).
aero_coefficients aero_coefficients_at(const ball_aero& aero, const air_state& air, float radius, float speed, float spin);

glm::vec3 gravity_acceleration();
// Drag and lift on a ball of `radius` moving at `velocity` through still
// `air` (the wind already taken off), spinning `spin`: drag against the
// velocity, lift across it along spin x velocity. Drag is clamped so one
// step of `dt` can at most stop the ball, never reverse it.
glm::vec3 aero_acceleration(const ball_aero& aero, const air_state& air, float radius, const glm::vec3& velocity,
                            const glm::vec3& spin, float dt);
