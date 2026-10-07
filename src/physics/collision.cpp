#include "physics/collision.h"

#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

float contact_friction_keep(const float friction, const float dt) {
    const float per_reference = 1.0f - clamp01(friction);
    return std::pow(per_reference, std::max(0.0f, dt) / contact_friction_reference_seconds);
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
