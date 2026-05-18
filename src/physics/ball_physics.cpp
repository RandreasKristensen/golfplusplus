#include "physics/ball_physics.h"

#include "physics/flight_model.h"

#include <algorithm>

ball_state step(const ball_state in, const wind_state wind, const float dt) {
    constexpr float drag_coeff = 0.02f;
    constexpr float magnus_coeff = 0.0004f;
    constexpr float spin_decay = 0.6f;

    const glm::vec3 relative_velocity = in.velocity - wind.velocity;
    const glm::vec3 accel = gravity_accel()
        + drag_accel(relative_velocity, drag_coeff, dt)
        + magnus_accel(in.spin, relative_velocity, magnus_coeff);

    ball_state out = in;
    out.velocity = in.velocity + accel * dt;
    // Semi-implicit Euler: update velocity before position for stability.
    out.position = in.position + out.velocity * dt;
    const float spin_factor = std::max(0.0f, 1.0f - spin_decay * dt);
    out.spin = in.spin * spin_factor;
    return out;
}
