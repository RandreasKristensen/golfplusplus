#include "physics/ball_physics.h"

#include "physics/flight_model.h"

#include <algorithm>

ball_state step_ball_flight(const ball_state& in, const wind_state& wind, const float dt, const physics_tuning& tuning) {
    const glm::vec3 relative_velocity = in.velocity - wind.velocity;
    const glm::vec3 acceleration = gravity_acceleration()
        + drag_acceleration(relative_velocity, tuning.drag_coeff, dt)
        + magnus_acceleration(in.spin, relative_velocity, tuning.magnus_coeff);

    ball_state out = in;
    // Semi-implicit Euler: velocity first, then position with the new velocity.
    out.velocity = in.velocity + acceleration * dt;
    out.position = in.position + out.velocity * dt;
    out.spin = in.spin * std::max(0.0f, 1.0f - tuning.spin_decay * dt);
    return out;
}
