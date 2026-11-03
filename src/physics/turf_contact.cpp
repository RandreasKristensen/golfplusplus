#include "physics/turf_contact.h"

#include "physics/det_math.h"
#include "physics/flight_model.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace {
// A golf ball's moment of inertia, as a share of m r^2: a solid sphere's.
// Slip at the contact point changes 1 + 1/k as fast as the ball's speed.
constexpr float inertia_share = 0.4f;
constexpr float slip_gain = 1.0f + 1.0f / inertia_share;
// Slower than this at the contact point it rolls.
constexpr float rolling_slip = 0.01f;
// The steepest Penner's contact angle gets (degrees): the turf gives, it does not fold.
constexpr float max_contact_angle_degrees = 40.0f;

glm::vec3 gravity() {
    return glm::vec3(0.0f, -gravity_meters_per_second2, 0.0f);
}

// The ball's velocity and spin after a tangential impulse (per unit mass)
// `impulse` at the contact point `to_contact` from its centre.
void apply_tangential(ball_state& ball, const glm::vec3& to_contact, const glm::vec3& impulse) {
    const float radius = std::max(1.0e-4f, ball.radius);
    ball.velocity += impulse;
    ball.spin += glm::cross(to_contact, impulse) / (inertia_share * radius * radius);
}

// How fast the contact point at `to_contact` slides over the ground, along it.
glm::vec3 contact_slip(const ball_state& ball, const glm::vec3& to_contact, const glm::vec3& normal) {
    const glm::vec3 point = ball.velocity + glm::cross(ball.spin, to_contact);
    return point - normal * glm::dot(point, normal);
}

void bounce(ball_state& ball, const glm::vec3& normal, const turf& surface) {
    const float speed = glm::length(ball.velocity);
    const float into = -glm::dot(ball.velocity, normal);
    const glm::vec3 along = ball.velocity + normal * into;
    const float along_speed = glm::length(along);
    // The crater's back wall: the plane tilted back towards the incoming ball.
    glm::vec3 face = normal;
    if (along_speed > 1.0e-4f && speed > 0.0f) {
        const float angle = det_atan2(into, along_speed);
        const float tilt = std::min(glm::radians(max_contact_angle_degrees),
                                    std::max(0.0f, surface.contact_angle_scale) * green_contact_angle(speed, angle));
        face = safe_normalize(normal * det_cos(tilt) - (along / along_speed) * det_sin(tilt), normal);
    }
    const float approach = glm::dot(ball.velocity, face);
    if (approach >= 0.0f) {
        face = normal;
    }
    const float closing = -glm::dot(ball.velocity, face);
    const float restitution = clamp01(std::max(0.0f, surface.restitution_scale) * green_restitution(speed));
    const float normal_impulse = (1.0f + restitution) * closing;
    ball.velocity += face * normal_impulse;
    // Friction at the contact point, as much as stops it sliding.
    const glm::vec3 to_contact = -face * ball.radius;
    const glm::vec3 slip = contact_slip(ball, to_contact, face);
    const float slip_speed = glm::length(slip);
    if (slip_speed > 0.0f) {
        const float impulse = std::min(std::max(0.0f, surface.friction) * normal_impulse, slip_speed / slip_gain);
        apply_tangential(ball, to_contact, -slip / slip_speed * impulse);
    }
}

// Along the ground: the slip worn away by friction, then rolling with the
// surface's resistance; a slope pulls a rolling ball at (5/7) g sin(slope).
bool roll(ball_state& ball, const glm::vec3& normal, const turf& surface, const float dt) {
    const float into = glm::dot(ball.velocity, normal);
    if (into < 0.0f) {
        ball.velocity -= normal * into;
    }
    const glm::vec3 to_contact = -normal * ball.radius;
    const glm::vec3 slip = contact_slip(ball, to_contact, normal);
    const float slip_speed = glm::length(slip);
    const float press = -glm::dot(gravity(), normal);
    const float wear = std::max(0.0f, surface.friction) * press * dt;
    if (slip_speed > rolling_slip && wear * slip_gain < slip_speed) {
        apply_tangential(ball, to_contact, -slip / slip_speed * wear);
        return false;
    }
    // Rolling: a rolling sphere takes only 5/7 of the slope's pull, which
    // the flight step gave in full.
    const glm::vec3 downhill = gravity() - normal * glm::dot(gravity(), normal);
    ball.velocity -= downhill * ((1.0f - 5.0f / 7.0f) * dt);
    glm::vec3 along = ball.velocity - normal * glm::dot(ball.velocity, normal);
    const float speed = glm::length(along);
    const float resistance =
        rolling_deceleration(speed, surface.stimp_feet) + std::max(0.0f, surface.grass_drag) * speed;
    const float slowed = speed - resistance * dt;
    bool resting = false;
    if (slowed <= 0.0f) {
        along = glm::vec3(0.0f);
        // It stays put where the slope's pull is less than what holds it.
        resting = (5.0f / 7.0f) * glm::length(downhill) <= rolling_deceleration(0.0f, surface.stimp_feet);
    } else {
        along *= slowed / speed;
    }
    ball.velocity = along;
    ball.spin = glm::cross(normal, along) / std::max(1.0e-4f, ball.radius);
    return resting;
}
}

float green_restitution(const float speed) {
    if (speed > 20.0f) {
        return 0.120f;
    }
    return 0.510f - 0.0375f * speed + 0.000903f * speed * speed;
}

float green_contact_angle(const float speed, const float angle) {
    return glm::radians(15.4f) * (speed / 18.6f) * (glm::degrees(angle) / 44.4f);
}

float rolling_deceleration(const float speed, const float stimp_feet) {
    if (stimp_feet <= 0.0f) {
        return 0.0f;
    }
    // Penner's speed-dependent resistance, its constant set so that a ball
    // rolling off a stimpmeter at 1.83 m/s rolls the reading's feet.
    constexpr float stimp_constant = 0.77559f;
    const float resistance = (stimp_constant / stimp_feet) * (1.0f + 0.0065f * speed * speed);
    return (5.0f / 7.0f) * resistance * gravity_meters_per_second2;
}

turf_step turf_contact(const ball_state& in, const terrain_sample& ground, const turf& surface, const float settle_speed,
                       const float dt) {
    turf_step out{in};
    const glm::vec3 normal = ground_normal(ground.normal);
    const float radius = std::max(0.0f, in.radius);
    const float distance = glm::dot(in.position - ground.point, normal);
    if (distance >= radius) {
        return out;
    }
    ball_state& ball = out.ball;
    ball.position += normal * (radius - distance);
    const float into = -glm::dot(ball.velocity, normal);
    if (into > settle_speed) {
        if (surface.plug_speed > 0.0f && into >= surface.plug_speed) {
            ball.position -= normal * std::max(0.0f, surface.plug_depth);
            ball.velocity = glm::vec3(0.0f);
            ball.spin = glm::vec3(0.0f);
            out.plugged = true;
            out.resting = true;
            return out;
        }
        bounce(ball, normal, surface);
        out.bounced = true;
        return out;
    }
    out.resting = roll(ball, normal, surface, dt);
    return out;
}
