#include "physics/flight_model.h"

#include "physics/det_math.h"
#include "physics/vector_math.h"

#include <algorithm>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

namespace {
constexpr float celsius_to_kelvin = 273.15f;
}

air_state air_at(const air_tuning& tuning, const float elevation, const float temperature_c) {
    const float exponent = gravity_meters_per_second2 / (tuning.gas_constant * tuning.lapse_rate);
    const float falloff = std::max(0.01f, 1.0f - tuning.lapse_rate * elevation / tuning.standard_temperature);
    const float pressure = tuning.sea_level_pressure * det_pow(falloff, exponent);
    return air_state{pressure / (tuning.gas_constant * (temperature_c + celsius_to_kelvin)), tuning.viscosity};
}

aero_coefficients aero_coefficients_at(const ball_aero& aero, const air_state& air, const float radius, const float speed,
                                       const float spin) {
    if (speed <= 0.0f) {
        return {};
    }
    const float spin_parameter = radius * spin / speed;
    const float reynolds = air.viscosity > 0.0f ? air.density * speed * 2.0f * radius / air.viscosity : 0.0f;
    const float crisis_span = std::max(1.0f, aero.crisis_re_end - aero.crisis_re_start);
    const float crisis = clamp01((reynolds - aero.crisis_re_start) / crisis_span);
    aero_coefficients out;
    const float s3 = spin_parameter * spin_parameter * spin_parameter;
    const float h = std::max(0.0001f, aero.lift_half_spin);
    out.lift = aero.lift_max * s3 / (s3 + h * h * h) * (1.0f - aero.lift_crisis * crisis);
    out.drag = std::max(0.0f, aero.drag_base + aero.drag_spin * spin_parameter * spin_parameter - aero.drag_crisis * crisis);
    return out;
}

glm::vec3 gravity_acceleration() {
    return glm::vec3(0.0f, -gravity_meters_per_second2, 0.0f);
}

glm::vec3 aero_acceleration(const ball_aero& aero, const air_state& air, const float radius, const glm::vec3& velocity,
                            const glm::vec3& spin, const float dt) {
    const float speed = glm::length(velocity);
    if (speed <= 0.0f || dt <= 0.0f || aero.mass <= 0.0f) {
        return glm::vec3(0.0f);
    }
    const float spin_rate = glm::length(spin);
    const aero_coefficients c = aero_coefficients_at(aero, air, radius, speed, spin_rate);
    const float area = glm::pi<float>() * radius * radius;
    // Half rho A v^2 / m: the dynamic pressure on the ball, per kilogram.
    const float per_coefficient = 0.5f * air.density * area * speed * speed / aero.mass;
    const float deceleration = std::min(per_coefficient * c.drag, speed / dt);
    const glm::vec3 along = velocity / speed;
    glm::vec3 acceleration = -along * deceleration;
    if (spin_rate > 0.0f) {
        const glm::vec3 across = glm::cross(spin / spin_rate, along);
        acceleration += across * (per_coefficient * c.lift);
    }
    return acceleration;
}
