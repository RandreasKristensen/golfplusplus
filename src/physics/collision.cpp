#include "physics/collision.h"

#include "physics/det_math.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

float contact_friction_keep(const float friction, const float dt) {
    const float per_reference = 1.0f - clamp01(friction);
    return exact_power(per_reference, std::max(0.0f, dt) / contact_friction_reference_seconds);
}

ball_state resolve_terrain_collision(const ball_state& in,
                                     const terrain_sample& terrain,
                                     const float restitution,
                                     const float friction,
                                     const float dt) {
    const glm::vec3 normal = ground_normal(terrain.normal);
    const float radius = std::max(0.0f, in.radius);
    const float distance = glm::dot(in.position - terrain.point, normal);
    if (distance >= radius) {
        return in;
    }

    ball_state out = in;
    out.position += normal * (radius - distance);
    const float normal_speed = glm::dot(out.velocity, normal);
    if (normal_speed < 0.0f) {
        out.velocity -= (1.0f + clamp01(restitution)) * normal_speed * normal;
    }

    const glm::vec3 normal_velocity = normal * glm::dot(out.velocity, normal);
    const glm::vec3 tangent_velocity = out.velocity - normal_velocity;
    out.velocity = normal_velocity + tangent_velocity * contact_friction_keep(friction, dt);
    return out;
}

ball_state resolve_contact(const ball_state& in,
                           const glm::vec3& normal,
                           const float penetration,
                           const float restitution,
                           const float friction) {
    if (penetration <= 0.0f) {
        return in;
    }

    ball_state out = in;
    const glm::vec3 n = safe_normalize(normal, glm::vec3(1.0f, 0.0f, 0.0f));
    out.position += n * penetration;

    const float normal_speed = glm::dot(out.velocity, n);
    if (normal_speed < 0.0f) {
        out.velocity -= (1.0f + clamp01(restitution)) * normal_speed * n;
    }

    const glm::vec3 normal_velocity = n * glm::dot(out.velocity, n);
    const glm::vec3 tangent_velocity = out.velocity - normal_velocity;
    out.velocity = normal_velocity + tangent_velocity * (1.0f - clamp01(friction));
    return out;
}

ball_state resolve_gripping_contact(const ball_state& in,
                                    const glm::vec3& normal,
                                    const float penetration,
                                    const float restitution,
                                    const float grip) {
    if (penetration <= 0.0f) {
        return in;
    }
    ball_state out = in;
    const glm::vec3 n = safe_normalize(normal, glm::vec3(1.0f, 0.0f, 0.0f));
    out.position += n * penetration;
    const float normal_speed = glm::dot(out.velocity, n);
    if (normal_speed >= 0.0f) {
        return out;
    }
    const float normal_impulse = -(1.0f + clamp01(restitution)) * normal_speed;
    out.velocity += n * normal_impulse;
    const float radius = std::max(1.0e-4f, out.radius);
    const glm::vec3 to_contact = -n * radius;
    const glm::vec3 point = out.velocity + glm::cross(out.spin, to_contact);
    const glm::vec3 slip = point - n * glm::dot(point, n);
    const float slip_speed = glm::length(slip);
    if (slip_speed > 0.0f) {
        // Slip changes 1 + 1/k as fast as the ball's speed.
        const float most = slip_speed / (1.0f + 1.0f / ball_inertia_share);
        const glm::vec3 impulse = -slip / slip_speed * std::min(std::max(0.0f, grip) * normal_impulse, most);
        out.velocity += impulse;
        out.spin += glm::cross(to_contact, impulse) / (ball_inertia_share * radius * radius);
    }
    return out;
}
