#pragma once

// Fences: a net hung between two poles, from the ground up to its height.
// The ball is swept along its step, so a fast shot cannot pass between the
// net's positions on two steps.

#include "physics/ball_state.h"

#include <vector>

#include <glm/vec3.hpp>

// One stretch of fence between neighbouring poles. `a` and `b` are the
// poles' feet; the net reaches `height` above the line between them.
struct fence_panel {
    glm::vec3 a{0.0f};
    glm::vec3 b{0.0f};
    float height = 0.0f;
};

// The ball after a step that took it from `previous_position` to
// `in.position`: stopped against the net where its path first comes within a
// ball radius of it, bounced by `restitution` and with `friction` of its
// speed along the net taken. Unchanged if it never reaches the net.
ball_state resolve_fence_collision(const glm::vec3& previous_position,
                                   const ball_state& in,
                                   const fence_panel& panel,
                                   float restitution,
                                   float friction);
ball_state resolve_fence_collisions(const glm::vec3& previous_position,
                                    const ball_state& in,
                                    const std::vector<fence_panel>& panels,
                                    float restitution,
                                    float friction);
