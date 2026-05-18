#include "physics/ball_physics.h"

#include "physics/flight_model.h"

ball_state step(const ball_state in, const wind_state wind, const float dt) {
    constexpr float drag_coeff = 0.02f;
    constexpr float magnus_coeff = 0.0004f;

    const glm::vec3 relative_velocity = in.velocity - wind.velocity;
    const glm::vec3 accel = gravity_accel()
        + drag_accel(relative_velocity, drag_coeff)
        + magnus_accel(in.spin, relative_velocity, magnus_coeff);

    ball_state out = in;
    out.velocity = in.velocity + accel * dt;
    out.position = in.position + out.velocity * dt;
    return out;
}
