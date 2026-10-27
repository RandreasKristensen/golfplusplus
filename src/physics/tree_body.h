#pragma once

// A tree as content places it and the course stands it: a cylinder trunk with
// a cone of leaves on top. What the ball and people meet is its solids
// (game/area_solids.h).

#include <glm/vec3.hpp>

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
