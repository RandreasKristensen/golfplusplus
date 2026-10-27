#include "physics/cart_dynamics.h"

#include "physics/flight_model.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace {
// Slower than this it counts as standing.
constexpr float still_speed = 0.05f;

glm::vec2 heading(const float yaw) {
    return glm::vec2(std::sin(yaw), std::cos(yaw));
}

glm::vec2 left_of(const float yaw) {
    return glm::vec2(std::cos(yaw), -std::sin(yaw));
}

float sign_of(const float value) {
    return value < 0.0f ? -1.0f : 1.0f;
}

// The sine and cosine of a slope rising `rise` per metre.
struct slope {
    float sine = 0.0f;
    float cosine = 1.0f;
};

slope slope_of(const float rise) {
    const float length = std::sqrt(1.0f + rise * rise);
    return slope{rise / length, 1.0f / length};
}

cart_motion stopped(cart_motion motion) {
    motion.velocity = glm::vec2(0.0f);
    motion.yaw_rate = 0.0f;
    motion.lift_seconds = 0.0f;
    motion.load = 0.0f;
    return motion;
}

// Its speed along its heading after `dt`, and the share of the motor's force it drove with.
struct along_heading {
    float speed = 0.0f;
    float load = 0.0f;
};

along_heading drive_along(const float speed, const cart_controls& controls, const float gravity_along, const float resistance,
                          const cart_spec& spec, const float dt) {
    const float throttle = std::clamp(controls.throttle, -1.0f, 1.0f);
    const float mass = std::max(spec.mass_kg, 1.0f);
    const float most_force = std::min(spec.max_drive_force_n, spec.motor_power_w / std::max(std::abs(speed), still_speed));
    const bool driving_forward = throttle > 0.0f && speed > -still_speed;
    const bool driving_back = throttle < 0.0f && speed < still_speed;
    float force = 0.0f;
    if (driving_forward) {
        force = most_force * throttle;
    } else if (driving_back) {
        force = most_force * spec.reverse_share * throttle;
    } else if (std::abs(speed) < still_speed) {
        return along_heading{};  // held standing
    } else {
        force = -sign_of(speed) * spec.max_drive_force_n;  // braking
    }
    const float pushed = force / mass + gravity_along;
    along_heading out;
    out.load = driving_forward || driving_back ? std::abs(force) / std::max(spec.max_drive_force_n, 1.0f) : 0.0f;
    // Rolling resistance against the way it rolls, or holding it standing
    // while it is the stronger.
    if (std::abs(speed) < still_speed && std::abs(pushed) <= resistance) {
        return out;
    }
    const float rolling = std::abs(speed) >= still_speed ? sign_of(speed) : sign_of(pushed);
    out.speed = speed + (pushed - rolling * resistance) * dt;
    if (out.speed * rolling < 0.0f) {
        out.speed = 0.0f;  // stopped this step; the next starts from standing
    }
    if (!driving_forward && !driving_back && out.speed * speed < 0.0f) {
        out.speed = 0.0f;  // the brake stops it, never backs it
    }
    // The governor, either way.
    out.speed = std::clamp(out.speed, -spec.top_speed_mps * spec.reverse_share, spec.top_speed_mps);
    return out;
}
}

float cart_forward_speed(const cart_motion& motion) {
    return glm::dot(motion.velocity, heading(motion.yaw));
}

float cart_stability(const cart_spec& spec) {
    return spec.track_m / (2.0f * std::max(spec.cg_height_m, 0.01f));
}

cart_motion step_cart(const cart_motion& in, const cart_controls& controls, const cart_ground& ground, const cart_spec& spec,
                      const float dt) {
    if (in.condition != cart_condition::upright) {
        return stopped(in);
    }
    if (ground.water_depth > spec.wade_depth_m) {
        cart_motion sunk = stopped(in);
        sunk.condition = cart_condition::sunk;
        return sunk;
    }
    cart_motion out = in;
    out.load = 0.0f;
    if (!ground.on_ground) {
        out.yaw_rate = 0.0f;  // in the air it flies on as it left
        return out;
    }
    const float g = gravity_meters_per_second2;
    const glm::vec3 normal = ground_normal(ground.normal);
    const glm::vec2 forward = heading(in.yaw);
    const glm::vec2 left = left_of(in.yaw);
    const glm::vec2 rise = -glm::vec2(normal.x, normal.z) / std::max(normal.y, 0.05f);
    const slope ahead = slope_of(glm::dot(rise, forward));
    const slope side = slope_of(glm::dot(rise, left));  // rising to the left

    const along_heading along = drive_along(glm::dot(in.velocity, forward), controls, -g * ahead.sine,
                                            ground.rolling_resistance * g * ahead.cosine, spec, dt);
    const float speed = along.speed;
    out.load = along.load;

    // The steering asks for a turn; the tyres give it as far as their grip goes.
    const float grip = std::max(ground.grip, 0.0f) * g * side.cosine;
    const float wheels = std::clamp(controls.steer, -1.0f, 1.0f) * glm::radians(spec.max_steer_degrees);
    float yaw_rate = speed * std::tan(wheels) / std::max(spec.wheelbase_m, 0.1f);
    const float turning = std::abs(yaw_rate * speed);
    if (turning > grip) {
        yaw_rate *= grip / turning;
    }

    // Sideways the slope pulls it down, and what grip the turn leaves holds it.
    float sideways = glm::dot(in.velocity, left) - g * side.sine * dt;
    const float hold = std::max(grip - std::abs(yaw_rate * speed), 0.0f) * dt;
    sideways = std::abs(sideways) <= hold ? 0.0f : sideways - sign_of(sideways) * hold;

    // The turn pulls it outwards (to the right turning left) and the slope
    // downhill; together past its stability its uphill wheels lift.
    const float pull_right = yaw_rate * speed + g * side.sine;
    if (std::abs(pull_right) > cart_stability(spec) * g * side.cosine) {
        out.lift_seconds = in.lift_seconds + dt;
    } else {
        out.lift_seconds = 0.0f;
    }
    if (out.lift_seconds >= spec.tip_seconds) {
        cart_motion tipped = stopped(out);
        tipped.condition = pull_right > 0.0f ? cart_condition::on_right_side : cart_condition::on_left_side;
        return tipped;
    }

    out.yaw = wrap_angle(in.yaw + yaw_rate * dt);
    out.yaw_rate = yaw_rate;
    out.velocity = heading(out.yaw) * speed + left_of(out.yaw) * sideways;
    return out;
}
