#pragma once

// GL-free: the trees as the instanced draws take them (unit tested). Each
// tree is one instance of its species' grown model (renderer/tree_model.h):
// the model's unit frame stood at the trunk's foot, scaled to the crown's
// radius across and the tree's height up, turned about its trunk, with a
// tint to tell neighbours apart. Each frame the trees in view are sorted
// into buckets, one per species and variant. Every pass uses the full model.

#include "physics/tree_body.h"
#include "renderer/frustum.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

// world = offset + turned_about_y(local * scale, yaw). Eight packed floats,
// uploaded straight into a GL instance buffer, shared by picture and shadows.
struct render_tree_instance {
    glm::vec3 offset{0.0f};
    glm::vec3 scale{1.0f};
    float yaw = 0.0f;
    float tint = 0.0f;  // 0..1
};

// Grown models per species, so a row of one species is not one tree repeated.
inline constexpr int tree_variants = 3;

struct render_tree_bucket {
    std::size_t species = 0;
    int variant = 0;
    std::size_t first = 0;  // into render_tree_batch::instances
    std::size_t count = 0;
};

struct render_tree_batch {
    std::vector<render_tree_instance> instances;  // bucket by bucket
    std::vector<render_tree_bucket> buckets;      // none empty
};

render_tree_instance tree_instance_for(const tree_body& tree);

// Which of its species' models a tree is, fixed by where it stands.
int tree_variant_for(const tree_body& tree);

// Trees within `frustum` (all when null), identically bucketed for every pass.
render_tree_batch build_tree_batch(const std::vector<tree_body>& trees, const view_frustum* frustum);

// A unit-frame point of a tree as it stands, as tree.vert places it.
glm::vec3 tree_world_point(const render_tree_instance& instance, const glm::vec3& local);
