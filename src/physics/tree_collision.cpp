#include "physics/tree_collision.h"

#include "physics/vector_math.h"

#include <algorithm>

namespace {
// Leaves push the ball slightly upwards so it drops out of the canopy.
constexpr float leaf_lift = 0.18f;

glm::vec3 horizontal_push_direction(const ball_state& ball, const glm::vec3& base) {
    const glm::vec3 away = horizontal(ball.position - base);
    const glm::vec3 backwards = horizontal(-ball.velocity);
    return safe_normalize(away, safe_normalize(backwards, glm::vec3(1.0f, 0.0f, 0.0f)));
}

// Tree hits are single impacts, so friction is a one-off fraction of the
// tangential speed (unlike terrain contact, which lasts several steps).
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

ball_state resolve_trunk_collision(const ball_state& in,
                                   const tree_body& tree,
                                   const float restitution,
                                   const float friction) {
    const float ball_radius = std::max(0.0f, in.radius);
    const float reach = std::max(0.0f, tree.shape.trunk_radius) + ball_radius;
    const float min_y = tree.base.y - ball_radius;
    const float max_y = tree.base.y + std::max(0.0f, tree.shape.trunk_height) + ball_radius;
    if (in.position.y < min_y || in.position.y > max_y) {
        return in;
    }

    const float distance = horizontal_distance(in.position, tree.base);
    if (distance >= reach) {
        return in;
    }
    return resolve_contact(in, horizontal_push_direction(in, tree.base), reach - distance, restitution, friction);
}

ball_state resolve_leaf_collision(const ball_state& in,
                                  const tree_body& tree,
                                  const float restitution,
                                  const float friction) {
    const float leaf_height = std::max(0.0f, tree.shape.leaf_height);
    if (leaf_height <= 0.00001f) {
        return in;
    }

    const float ball_radius = std::max(0.0f, in.radius);
    const float leaf_base_y = tree.base.y + std::max(0.0f, tree.shape.trunk_height);
    const float leaf_top_y = leaf_base_y + leaf_height;
    if (in.position.y < leaf_base_y - ball_radius || in.position.y > leaf_top_y + ball_radius) {
        return in;
    }

    // The canopy is a cone: widest at its base, a point at the top.
    const float height_in_cone = std::clamp(in.position.y - leaf_base_y, 0.0f, leaf_height);
    const float cone_radius = std::max(0.0f, tree.shape.leaf_radius) * (1.0f - height_in_cone / leaf_height);
    const float reach = cone_radius + ball_radius;

    const float distance = horizontal_distance(in.position, tree.base);
    if (distance >= reach) {
        return in;
    }

    const glm::vec3 push = horizontal_push_direction(in, tree.base);
    const glm::vec3 normal = safe_normalize(push + glm::vec3(0.0f, leaf_lift, 0.0f), push);
    return resolve_contact(in, normal, reach - distance, restitution, friction);
}
}

ball_state resolve_tree_collision(const ball_state& in,
                                  const tree_body& tree,
                                  const float restitution,
                                  const float friction) {
    return resolve_leaf_collision(resolve_trunk_collision(in, tree, restitution, friction),
                                  tree,
                                  restitution,
                                  friction);
}

ball_state resolve_tree_collisions(const ball_state& in,
                                   const std::vector<tree_body>& trees,
                                   const float restitution,
                                   const float friction) {
    ball_state out = in;
    for (const tree_body& tree : trees) {
        out = resolve_tree_collision(out, tree, restitution, friction);
    }
    return out;
}
