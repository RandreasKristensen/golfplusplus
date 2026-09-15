#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include <glm/vec3.hpp>

// GL-free tree render data. Kept separate from renderer.h so the per-instance
// builder can be unit tested without SDL or an OpenGL context.

struct render_tree {
    glm::vec3 base = glm::vec3(0.0f);
    float trunk_radius = 0.35f;
    float trunk_height = 2.4f;
    float leaf_radius = 1.6f;
    float leaf_height = 3.2f;
};

// Per-instance data for a unit mesh (cylinder or cone spanning y in [0, 1],
// radius 1). The instance model matrix is translate(offset) * scale(scale),
// i.e. world = offset + local * scale. Laid out as six tightly packed floats
// because it is uploaded straight into a GL instance buffer.
struct render_tree_instance {
    glm::vec3 offset = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
};

struct render_tree_instance_batch {
    std::vector<render_tree_instance> trunks;
    std::vector<render_tree_instance> leaves;
};

// Every dimension is clamped so degenerate authoring data never produces a
// zero scale (which would also break the inverse-transpose normal transform).
inline constexpr float min_tree_dimension = 0.01f;

// One trunk and one leaf instance per tree, in input order.
inline render_tree_instance_batch build_tree_instances(const std::vector<render_tree>& trees) {
    render_tree_instance_batch batch;
    batch.trunks.reserve(trees.size());
    batch.leaves.reserve(trees.size());

    for (const render_tree& tree : trees) {
        const float trunk_radius = std::max(min_tree_dimension, tree.trunk_radius);
        const float trunk_height = std::max(min_tree_dimension, tree.trunk_height);
        const float leaf_radius = std::max(min_tree_dimension, tree.leaf_radius);
        const float leaf_height = std::max(min_tree_dimension, tree.leaf_height);

        render_tree_instance trunk;
        trunk.offset = tree.base;
        trunk.scale = glm::vec3(trunk_radius, trunk_height, trunk_radius);
        batch.trunks.push_back(trunk);

        render_tree_instance leaf;
        leaf.offset = tree.base + glm::vec3(0.0f, trunk_height, 0.0f);
        leaf.scale = glm::vec3(leaf_radius, leaf_height, leaf_radius);
        batch.leaves.push_back(leaf);
    }

    return batch;
}
