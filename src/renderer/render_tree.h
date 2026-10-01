#pragma once

// GL-free per-instance data for the instanced tree draw (unit tested).

#include "physics/tree_collision.h"
#include "renderer/render_mesh.h"

#include <vector>

#include <glm/vec3.hpp>

// Per-instance data for a unit mesh (cylinder or cone spanning y in [0, 1],
// radius 1): world = offset + local * scale. Six packed floats, uploaded
// straight into a GL instance buffer.
struct render_tree_instance {
    glm::vec3 offset{0.0f};
    glm::vec3 scale{1.0f};
};

struct render_tree_instance_batch {
    std::vector<render_tree_instance> trunks;
    std::vector<render_tree_instance> leaves;
};

// One trunk and one leaf instance per tree, in input order. Tree sizes are
// already validated by the hole loader (never zero).
render_tree_instance_batch build_tree_instances(const std::vector<tree_body>& trees);

// World bounds of every instance (unit meshes span x/z in [-1, 1], y in
// [0, 1]). Invalid for an empty batch. Used for a whole-batch frustum cull.
render_mesh_bounds compute_tree_instance_bounds(const render_tree_instance_batch& batch);
