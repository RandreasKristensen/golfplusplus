#include "renderer/render_tree.h"

#include <glm/common.hpp>

render_tree_instance_batch build_tree_instances(const std::vector<tree_body>& trees) {
    render_tree_instance_batch batch;
    batch.trunks.reserve(trees.size());
    batch.leaves.reserve(trees.size());
    for (const tree_body& tree : trees) {
        const tree_shape& shape = tree.shape;
        batch.trunks.push_back(render_tree_instance{tree.base, glm::vec3(shape.trunk_radius, shape.trunk_height, shape.trunk_radius)});
        batch.leaves.push_back(render_tree_instance{tree.base + glm::vec3(0.0f, shape.trunk_height, 0.0f),
                                                    glm::vec3(shape.leaf_radius, shape.leaf_height, shape.leaf_radius)});
    }
    return batch;
}

render_mesh_bounds compute_tree_instance_bounds(const render_tree_instance_batch& batch) {
    render_mesh_bounds bounds;
    for (const std::vector<render_tree_instance>* instances : {&batch.trunks, &batch.leaves}) {
        for (const render_tree_instance& instance : *instances) {
            const glm::vec3 low = instance.offset - glm::vec3(instance.scale.x, 0.0f, instance.scale.z);
            const glm::vec3 high = instance.offset + instance.scale;
            bounds.min = bounds.valid ? glm::min(bounds.min, low) : low;
            bounds.max = bounds.valid ? glm::max(bounds.max, high) : high;
            bounds.valid = true;
        }
    }
    return bounds;
}
