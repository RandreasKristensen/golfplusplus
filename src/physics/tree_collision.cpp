#include "physics/tree_collision.h"

#include <algorithm>
#include <cmath>

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

glm::vec3 horizontal_fallback_normal(const ball_state& ball, const glm::vec3& base) {
    const glm::vec3 away(ball.position.x - base.x, 0.0f, ball.position.z - base.z);
    const glm::vec3 from_velocity(-ball.velocity.x, 0.0f, -ball.velocity.z);
    return safe_normalize(away, safe_normalize(from_velocity, glm::vec3(1.0f, 0.0f, 0.0f)));
}

ball_state resolve_contact(const ball_state& in,
                           const glm::vec3& normal,
                           const float penetration,
                           const float restitution,
                           const float friction) {
    ball_state out = in;
    if (penetration <= 0.0f) {
        return out;
    }

    const glm::vec3 n = safe_normalize(normal, glm::vec3(1.0f, 0.0f, 0.0f));
    out.position += n * penetration;

    const float normal_speed = glm::dot(out.velocity, n);
    if (normal_speed < 0.0f) {
        out.velocity = out.velocity - (1.0f + clamp01(restitution)) * normal_speed * n;
    }

    const float updated_normal_speed = glm::dot(out.velocity, n);
    const glm::vec3 normal_velocity = n * updated_normal_speed;
    const glm::vec3 tangent_velocity = out.velocity - normal_velocity;
    out.velocity = normal_velocity + tangent_velocity * (1.0f - clamp01(friction));
    return out;
}

ball_state resolve_trunk_collision(const ball_state& in,
                                   const tree_collision_body& tree,
                                   const float restitution,
                                   const float friction) {
    const float radius = std::max(0.0f, tree.trunk_radius) + std::max(0.0f, in.radius);
    const float min_y = tree.base.y - std::max(0.0f, in.radius);
    const float max_y = tree.base.y + std::max(0.0f, tree.trunk_height) + std::max(0.0f, in.radius);
    if (in.position.y < min_y || in.position.y > max_y) {
        return in;
    }

    const glm::vec3 delta(in.position.x - tree.base.x, 0.0f, in.position.z - tree.base.z);
    const float distance = glm::length(delta);
    if (distance >= radius) {
        return in;
    }

    const glm::vec3 normal = distance > 0.00001f
        ? delta / distance
        : horizontal_fallback_normal(in, tree.base);
    return resolve_contact(in, normal, radius - distance, restitution, friction);
}

ball_state resolve_leaf_collision(const ball_state& in,
                                  const tree_collision_body& tree,
                                  const float restitution,
                                  const float friction) {
    const float leaf_height = std::max(0.0f, tree.leaf_height);
    if (leaf_height <= 0.00001f) {
        return in;
    }

    const float ball_radius = std::max(0.0f, in.radius);
    const float leaf_base_y = tree.base.y + std::max(0.0f, tree.trunk_height);
    const float leaf_top_y = leaf_base_y + leaf_height;
    if (in.position.y < leaf_base_y - ball_radius || in.position.y > leaf_top_y + ball_radius) {
        return in;
    }

    const float local_y = std::max(0.0f, std::min(leaf_height, in.position.y - leaf_base_y));
    const float cone_t = local_y / leaf_height;
    const float cone_radius = std::max(0.0f, tree.leaf_radius) * (1.0f - cone_t);
    const float expanded_radius = cone_radius + ball_radius;

    const glm::vec3 delta(in.position.x - tree.base.x, 0.0f, in.position.z - tree.base.z);
    const float distance = glm::length(delta);
    if (distance >= expanded_radius) {
        return in;
    }

    const glm::vec3 horizontal_normal = distance > 0.00001f
        ? delta / distance
        : horizontal_fallback_normal(in, tree.base);
    const glm::vec3 normal = safe_normalize(horizontal_normal + glm::vec3(0.0f, 0.18f, 0.0f), horizontal_normal);
    return resolve_contact(in, normal, expanded_radius - distance, restitution, friction);
}
}

ball_state resolve_tree_collision(const ball_state in,
                                  const tree_collision_body& tree,
                                  const float restitution,
                                  const float friction) {
    return resolve_leaf_collision(resolve_trunk_collision(in, tree, restitution, friction),
                                  tree,
                                  restitution,
                                  friction);
}

ball_state resolve_tree_collisions(const ball_state in,
                                   const std::vector<tree_collision_body>& trees,
                                   const float restitution,
                                   const float friction) {
    ball_state out = in;
    for (const tree_collision_body& tree : trees) {
        out = resolve_tree_collision(out, tree, restitution, friction);
    }
    return out;
}
