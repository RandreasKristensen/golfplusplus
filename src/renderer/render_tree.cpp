#include "renderer/render_tree.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

#include <glm/geometric.hpp>

namespace {
// 0..1, fixed for a place.
float spot_hash(const float x, const float z, const float salt) {
    const float h = std::sin(x * 12.9898f + z * 78.233f + salt * 37.719f) * 43758.5453f;
    return h - std::floor(h);
}
}

render_tree_instance tree_instance_for(const tree_body& tree) {
    const tree_shape& shape = tree.shape;
    render_tree_instance instance;
    instance.offset = tree.base;
    const float height = std::max(0.0f, shape.trunk_height) + shape.leaf_height;
    instance.scale = glm::vec3(shape.leaf_radius, height, shape.leaf_radius);
    instance.yaw = 6.2831853f * spot_hash(tree.base.x, tree.base.z, 1.0f);
    instance.tint = spot_hash(tree.base.x, tree.base.z, 2.0f);
    return instance;
}

int tree_variant_for(const tree_body& tree) {
    return std::min(tree_variants - 1, static_cast<int>(spot_hash(tree.base.x, tree.base.z, 3.0f) * tree_variants));
}

glm::vec3 tree_world_point(const render_tree_instance& instance, const glm::vec3& local) {
    const glm::vec3 scaled = local * instance.scale;
    const float c = std::cos(instance.yaw);
    const float s = std::sin(instance.yaw);
    return instance.offset + glm::vec3(c * scaled.x - s * scaled.z, scaled.y, s * scaled.x + c * scaled.z);
}

render_tree_batch build_tree_batch(const std::vector<tree_body>& trees, const view_frustum* frustum) {
    std::map<std::tuple<std::size_t, int>, std::vector<render_tree_instance>> sorted;
    for (const tree_body& tree : trees) {
        const render_tree_instance instance = tree_instance_for(tree);
        // Cards extend past the unit crown and its top; allow wind lean too.
        const float sway = instance.scale.y * 0.2f;
        const glm::vec3 reach(instance.scale.x * 1.5f + sway, 0.0f, instance.scale.z * 1.5f + sway);
        if (frustum != nullptr &&
            !frustum_intersects_aabb(*frustum, tree.base - reach, tree.base + reach + glm::vec3(0.0f, instance.scale.y * 1.3f, 0.0f))) {
            continue;
        }
        sorted[{tree.shape.species, tree_variant_for(tree)}].push_back(instance);
    }
    render_tree_batch batch;
    for (auto& [key, instances] : sorted) {
        render_tree_bucket bucket;
        bucket.species = std::get<0>(key);
        bucket.variant = std::get<1>(key);
        bucket.first = batch.instances.size();
        bucket.count = instances.size();
        batch.instances.insert(batch.instances.end(), instances.begin(), instances.end());
        batch.buckets.push_back(bucket);
    }
    return batch;
}
