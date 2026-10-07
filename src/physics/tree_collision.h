#pragma once

#include "physics/ball_state.h"

#include <cstdint>
#include <vector>

#include <glm/vec2.hpp>
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

// The indices of `trees` bucketed by where they stand on a square grid, so a
// ball is tested only against the trees near it. Built once per shot.
struct tree_grid {
    float cell_size = 0.0f;
    float reach = 0.0f;  // the furthest any tree reaches from its base, horizontally
    glm::vec2 origin{0.0f};  // the x/z corner of cell (0, 0)
    int columns = 0;
    int rows = 0;
    std::vector<std::vector<std::uint32_t>> cells;  // row-major; each ascending
};

tree_grid build_tree_grid(const std::vector<tree_body>& trees, float cell_size);

// The same result as testing every tree (a tree out of reach leaves the ball
// as it is), testing only those near the ball, in their order in `trees`.
ball_state resolve_tree_collisions(const ball_state& in,
                                   const std::vector<tree_body>& trees,
                                   const tree_grid& grid,
                                   float restitution,
                                   float friction);
