#pragma once

// A tree as content places it and the course stands it: a cylinder trunk with
// a cone of leaves on top. What the ball and people meet is its solids
// (game/area_solids.h).

#include <cstdint>

#include <glm/vec3.hpp>

// A tree stands on its trunk; a bush is all leaves from the ground, low and
// dense (hole.json's "bushes": no trunk).
enum class tree_kind : std::uint8_t {
    tree,
    bush
};

struct tree_shape {
    float trunk_radius = 0.0f;
    float trunk_height = 0.0f;
    float leaf_radius = 0.0f;
    float leaf_height = 0.0f;
    tree_kind kind = tree_kind::tree;
    // Its species (an index into the tuning's trees, 0 the default) and the
    // share of its crown in leaf on the course's date: what slows a ball.
    std::uint8_t species = 0;
    float foliage = 1.0f;
};

// A tree placed on the terrain: `base` is the trunk's foot.
struct tree_body {
    glm::vec3 base{0.0f};
    tree_shape shape;
};
