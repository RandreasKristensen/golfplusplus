#include "physics/collision.h"

#include <algorithm>

#include <glm/geometric.hpp>

namespace {
float clamp01(const float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

float safe_ball_radius(const ball_state& ball) {
    return std::max(0.0f, ball.radius);
}

glm::vec3 safe_normalize(const glm::vec3& value, const glm::vec3& fallback) {
    const float len = glm::length(value);
    if (len <= 0.00001f) {
        return fallback;
    }
    return value / len;
}

float support_distance(const ball_state& ball, const glm::vec3& point, const glm::vec3& normal) {
    return glm::dot(ball.position - point, safe_normalize(normal, glm::vec3(0.0f, 1.0f, 0.0f)));
}
}

ball_state resolve_ground_collision(const ball_state in, const float ground_y, const float restitution, const float friction) {
    ball_state out = in;
    const float contact_y = ground_y + safe_ball_radius(out);
    if (out.position.y >= contact_y) {
        return out;
    }

    out.position.y = contact_y;
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
    const glm::vec3 normal = safe_normalize(terrain.normal, glm::vec3(0.0f, 1.0f, 0.0f));
    const float radius = safe_ball_radius(out);
    const float distance = support_distance(out, terrain.point, normal);
    if (distance >= radius) {
        return out;
    }

    out.position += normal * (radius - distance);
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
