#pragma once

#include "physics/ball_state.h"

#include <vector>

#include <glm/vec3.hpp>

struct tree_collision_body {
    glm::vec3 base{0.0f};
    float trunk_radius = 0.35f;
    float trunk_height = 2.4f;
    float leaf_radius = 1.6f;
    float leaf_height = 3.2f;
};

ball_state resolve_tree_collision(const ball_state in,
                                  const tree_collision_body& tree,
                                  float restitution,
                                  float friction);
ball_state resolve_tree_collisions(const ball_state in,
                                   const std::vector<tree_collision_body>& trees,
                                   float restitution,
                                   float friction);
