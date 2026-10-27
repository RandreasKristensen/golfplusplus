#include "physics/ball_physics.h"

#include <algorithm>

#include <glm/geometric.hpp>

namespace {
struct derivative {
    glm::vec3 acceleration{0.0f};
    float spin_loss = 0.0f;  // share of spin lost per second
};

derivative rates(const glm::vec3& velocity, const glm::vec3& spin, const float radius, const wind_state& wind,
                 const air_state& air, const bool under_water, const float dt, const physics_tuning& tuning) {
    derivative out;
    out.acceleration =
        gravity_acceleration() + aero_acceleration(tuning.aero, air, radius, velocity - wind.velocity, spin, dt);
    const float speed = glm::length(velocity);
    out.spin_loss = tuning.aero.spin_decay * speed / std::max(0.0001f, radius);
    if (under_water && speed > 0.0f) {
        out.acceleration -= velocity * std::min(tuning.water_drag_coeff * speed, 1.0f / dt);
        out.spin_loss += tuning.water_spin_decay;
    }
    return out;
}
}

ball_state step_ball_flight(const ball_state& in, const wind_state& wind, const air_state& air, const bool under_water,
                            const float dt, const physics_tuning& tuning) {
    // Midpoint (RK2): the forces at half a step on, from the forces now.
    const derivative now = rates(in.velocity, in.spin, in.radius, wind, air, under_water, dt, tuning);
    const float half = 0.5f * dt;
    const glm::vec3 mid_velocity = in.velocity + now.acceleration * half;
    const glm::vec3 mid_spin = in.spin * std::max(0.0f, 1.0f - now.spin_loss * half);
    const derivative mid = rates(mid_velocity, mid_spin, in.radius, wind, air, under_water, dt, tuning);

    ball_state out = in;
    out.velocity = in.velocity + mid.acceleration * dt;
    out.position = in.position + mid_velocity * dt;
    out.spin = in.spin * std::max(0.0f, 1.0f - mid.spin_loss * dt);
    return out;
}
