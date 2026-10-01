#pragma once

#include "physics/ball_state.h"

#include <vector>

#include <glm/vec3.hpp>

// A tree is a cylinder trunk with a cone of leaves on top.
struct tree_shape {
    float trunk_radius = 0.0f;
    float trunk_height = 0.0f;
    float leaf_radius = 0.0f;
    float leaf_height = 0.0f;
};

// A tree placed on the terrain: `base` is the trunk's foot.
struct tree_body {
    glm::vec3 base{0.0f};
    tree_shape shape;
};

ball_state resolve_tree_collision(const ball_state& in,
                                  const tree_body& tree,
                                  float restitution,
                                  float friction);
ball_state resolve_tree_collisions(const ball_state& in,
                                   const std::vector<tree_body>& trees,
                                   float restitution,
                                   float friction);
