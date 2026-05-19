#include "physics/collision.h"

#include <algorithm>

#include <glm/geometric.hpp>

namespace {
float clamp01(const float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

glm::vec3 safe_normalize(const glm::vec3& value, const glm::vec3& fallback) {
    const float len = glm::length(value);
    if (len <= 0.00001f) {
        return fallback;
    }
    return value / len;
}
}

ball_state resolve_ground_collision(const ball_state in, const float ground_y, const float restitution, const float friction) {
    ball_state out = in;
    if (out.position.y >= ground_y) {
        return out;
    }

    out.position.y = ground_y;
    const float clamped_restitution = clamp01(restitution);
    if (out.velocity.y < 0.0f) {
        out.velocity.y = -out.velocity.y * clamped_restitution;
    }

    const float clamped_friction = clamp01(friction);
    out.velocity.x *= 1.0f - clamped_friction;
    out.velocity.z *= 1.0f - clamped_friction;

    return out;
}

ball_state resolve_terrain_collision(const ball_state in,
                                     const terrain_sample& terrain,
                                     const float restitution,
                                     const float friction) {
    ball_state out = in;
    if (out.position.y >= terrain.point.y) {
        return out;
    }

    out.position.y = terrain.point.y;

    const glm::vec3 normal = safe_normalize(terrain.normal, glm::vec3(0.0f, 1.0f, 0.0f));
    const float normal_speed = glm::dot(out.velocity, normal);
    if (normal_speed < 0.0f) {
        const float clamped_restitution = clamp01(restitution);
        out.velocity = out.velocity - (1.0f + clamped_restitution) * normal_speed * normal;
    }

    const float clamped_friction = clamp01(friction);
    const float updated_normal_speed = glm::dot(out.velocity, normal);
    const glm::vec3 normal_velocity = normal * updated_normal_speed;
    const glm::vec3 tangent_velocity = out.velocity - normal_velocity;
    out.velocity = normal_velocity + tangent_velocity * (1.0f - clamped_friction);

    return out;
}
