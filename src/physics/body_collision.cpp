#include "physics/body_collision.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
// Pushes per step: enough to settle a body caught between two obstacles.
constexpr int clear_passes = 3;
// However far one move goes, it takes at most this many steps.
constexpr int max_steps = 64;

glm::vec2 flat(const glm::vec3& point) {
    return glm::vec2(point.x, point.z);
}

// How far a circle at `center` must move to be `reach` from `point`.
glm::vec2 push_from(const glm::vec2& center, const glm::vec2& point, const float reach) {
    const glm::vec2 away = center - point;
    const float distance = glm::length(away);
    if (distance >= reach) {
        return glm::vec2(0.0f);
    }
    const glm::vec2 direction = distance > 1.0e-5f ? away / distance : glm::vec2(1.0f, 0.0f);
    return direction * (reach - distance);
}

glm::vec2 nearest_on_wall(const upright_wall& wall, const glm::vec2& point) {
    const glm::vec2 span = wall.b - wall.a;
    const float length_squared = glm::dot(span, span);
    if (length_squared <= 1.0e-10f) {
        return wall.a;
    }
    return wall.a + span * std::clamp(glm::dot(point - wall.a, span) / length_squared, 0.0f, 1.0f);
}

// How far a circle at `center` of `radius` must move to be clear of `block`:
// out through its nearest side when its centre is inside.
glm::vec2 push_from_block(const glm::vec2& center, const float radius, const upright_block& block) {
    const glm::vec2 across(block.along.y, -block.along.x);
    const glm::vec2 local = center - block.center;
    const float u = glm::dot(local, block.along);
    const float v = glm::dot(local, across);
    if (std::abs(u) < block.half_length && std::abs(v) < block.half_width) {
        const float out_along = block.half_length - std::abs(u);
        const float out_across = block.half_width - std::abs(v);
        return out_along < out_across ? block.along * (std::copysign(out_along, u) + std::copysign(radius, u))
                                      : across * (std::copysign(out_across, v) + std::copysign(radius, v));
    }
    const glm::vec2 nearest = block.center + block.along * std::clamp(u, -block.half_length, block.half_length) +
        across * std::clamp(v, -block.half_width, block.half_width);
    return push_from(center, nearest, radius);
}

glm::vec2 push_clear(const upright_obstacles& obstacles, const glm::vec2& center, const float radius) {
    glm::vec2 at = center;
    for (const upright_post& post : obstacles.posts) {
        at += push_from(at, post.center, post.radius + radius);
    }
    for (const upright_wall& wall : obstacles.walls) {
        at += push_from(at, nearest_on_wall(wall, at), wall.half_thickness + radius);
    }
    for (const upright_block& block : obstacles.blocks) {
        at += push_from_block(at, radius, block);
    }
    return at - center;
}
}

body_move move_body(const std::vector<const upright_obstacles*>& obstacles, const ground_body& body,
                    const glm::vec3& from, const glm::vec3& to) {
    const glm::vec2 travel = flat(to) - flat(from);
    const float step_length = std::max(0.01f, 0.5f * body.radius);
    const int steps = std::clamp(static_cast<int>(std::ceil(glm::length(travel) / step_length)), 1, max_steps);
    glm::vec2 at = flat(from);
    bool touched = false;
    const glm::vec2 step_travel = travel / static_cast<float>(steps);
    const float friction = std::clamp(body.friction, 0.0f, 1.0f);
    for (int step = 0; step < steps; ++step) {
        const glm::vec2 stepped = at + step_travel;
        at = stepped;
        for (int pass = 0; pass < clear_passes; ++pass) {
            bool pushed = false;
            for (const glm::vec2& offset : body.offsets) {
                for (const upright_obstacles* set : obstacles) {
                    const glm::vec2 push = push_clear(*set, at + offset, body.radius);
                    if (push != glm::vec2(0.0f)) {
                        at += push;
                        pushed = true;
                    }
                }
            }
            touched = touched || pushed;
            if (!pushed) {
                break;
            }
        }
        // Of the step's motion along what it pressed against, friction
        // holds some back.
        const glm::vec2 push = at - stepped;
        const float push_length = glm::length(push);
        if (push_length > 0.0f && friction > 0.0f) {
            const glm::vec2 normal = push / push_length;
            at -= (step_travel - normal * glm::dot(step_travel, normal)) * friction;
        }
    }
    return body_move{glm::vec3(at.x, to.y, at.y), touched};
}
