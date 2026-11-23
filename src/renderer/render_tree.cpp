#include "renderer/render_tree.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
// 0..1, fixed for a place.
float spot_hash(const float x, const float z, const float salt) {
    const float h = std::sin(x * 12.9898f + z * 78.233f + salt * 37.719f) * 43758.5453f;
    return h - std::floor(h);
}

// Cards extend past the unit crown (reach), its top (height) and its foot.
constexpr float crown_reach = 1.5f;
constexpr float crown_height = 1.3f;
constexpr float crown_foot = 0.05f;
// tree_pose.glsl: wind * 0.004 * scale.y * local.y^2 * gust, gust at most 1,
// for the fastest wind a look may be set to (max_wind_speed, lighting_fields.h).
constexpr float strongest_wind = 60.0f;
constexpr float wind_bend = 0.004f;
// Cells of this many metres, in blocks of eight a side, test a view in two levels.
constexpr float cell_meters = 16.0f;
constexpr float block_meters = 128.0f;
// Instances between two runs of one bucket that a single draw takes in anyway.
constexpr std::size_t merged_gap_instances = 6;

struct placed_tree {
    std::array<int, 6> key{};  // species, variant, block z and x, cell z and x
    render_tree_instance instance;
    tree_bounds bounds;
};

int cell_of(const float meters, const float size) {
    return static_cast<int>(std::floor(meters / size));
}

void grow(tree_bounds& bounds, const tree_bounds& by) {
    bounds.min = glm::min(bounds.min, by.min);
    bounds.max = glm::max(bounds.max, by.max);
}

// Adds a run to `ranges`, joining it to the last when a gap of a few instances
// lies between, unless `other` ended inside that gap.
void append_run(std::vector<render_tree_bucket>& ranges, const std::vector<render_tree_bucket>& other,
                const render_tree_bucket& bucket, const std::size_t first, const std::size_t count) {
    if (!ranges.empty()) {
        render_tree_bucket& last = ranges.back();
        const std::size_t last_end = last.first + last.count;
        const bool other_in_gap = !other.empty() && other.back().species == bucket.species &&
            other.back().variant == bucket.variant && other.back().first + other.back().count > last_end;
        if (last.species == bucket.species && last.variant == bucket.variant &&
            first - last_end <= merged_gap_instances && !other_in_gap) {
            last.count = first + count - last.first;
            return;
        }
    }
    ranges.push_back({bucket.species, bucket.variant, first, count});
}

float distance_to_box(const glm::vec3& point, const tree_bounds& box) {
    return glm::length(glm::max(glm::max(box.min - point, point - box.max), glm::vec3(0.0f)));
}
}

float tree_image_error_pixels(const tree_lod_view& view, const float crown_radius, const float height,
                              const float distance) {
    if (distance <= 0.0f || view.focal_pixels <= 0.0f) {
        return std::numeric_limits<float>::infinity();
    }
    const float metres = view.radius_error * crown_radius + view.wind_error * view.wind_speed * height;
    return metres * view.focal_pixels / distance;
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

float tree_pose_reach(const render_tree_instance& instance) {
    return instance.scale.y * wind_bend * crown_height * crown_height;
}

tree_bounds tree_bounds_for(const render_tree_instance& instance) {
    const float sway = strongest_wind * tree_pose_reach(instance);
    const float reach = std::max(instance.scale.x, instance.scale.z) * crown_reach + sway;
    return {instance.offset - glm::vec3(reach, instance.scale.y * crown_foot, reach),
            instance.offset + glm::vec3(reach, instance.scale.y * crown_height, reach)};
}

glm::vec3 tree_world_point(const render_tree_instance& instance, const glm::vec3& local) {
    const glm::vec3 scaled = local * instance.scale;
    const float c = std::cos(instance.yaw);
    const float s = std::sin(instance.yaw);
    return instance.offset + glm::vec3(c * scaled.x - s * scaled.z, scaled.y, s * scaled.x + c * scaled.z);
}

void tree_index::build(const std::vector<tree_body>& trees) {
    instances_.clear();
    groups_.clear();
    blocks_.clear();
    cells_.clear();
    pose_reach_ = 0.0f;
    std::vector<placed_tree> placed;
    placed.reserve(trees.size());
    for (const tree_body& tree : trees) {
        placed_tree one;
        one.instance = tree_instance_for(tree);
        one.bounds = tree_bounds_for(one.instance);
        pose_reach_ = std::max(pose_reach_, tree_pose_reach(one.instance));
        one.key = {static_cast<int>(tree.shape.species), tree_variant_for(tree),
                   cell_of(tree.base.z, block_meters), cell_of(tree.base.x, block_meters),
                   cell_of(tree.base.z, cell_meters), cell_of(tree.base.x, cell_meters)};
        placed.push_back(one);
    }
    std::stable_sort(placed.begin(), placed.end(),
                     [](const placed_tree& a, const placed_tree& b) { return a.key < b.key; });
    instances_.reserve(placed.size());
    const std::array<int, 6>* previous = nullptr;
    for (const placed_tree& one : placed) {
        const bool new_group = previous == nullptr || one.key[0] != (*previous)[0] || one.key[1] != (*previous)[1];
        const bool new_block = new_group || one.key[2] != (*previous)[2] || one.key[3] != (*previous)[3];
        const bool new_cell = new_block || one.key[4] != (*previous)[4] || one.key[5] != (*previous)[5];
        if (new_group) {
            groups_.push_back({{static_cast<std::size_t>(one.key[0]), one.key[1], instances_.size(), 0}, blocks_.size(), 0});
        }
        if (new_block) {
            blocks_.push_back({one.bounds, cells_.size(), 0});
            ++groups_.back().block_count;
        } else {
            grow(blocks_.back().bounds, one.bounds);
        }
        if (new_cell) {
            cells_.push_back({one.bounds, instances_.size(), 0, 0.0f, 0.0f});
            ++blocks_.back().cell_count;
        } else {
            grow(cells_.back().bounds, one.bounds);
        }
        cells_.back().crown_radius = std::max(cells_.back().crown_radius, std::max(one.instance.scale.x, one.instance.scale.z));
        cells_.back().height = std::max(cells_.back().height, one.instance.scale.y);
        ++cells_.back().count;
        ++groups_.back().bucket.count;
        instances_.push_back(one.instance);
        previous = &one.key;
    }
}

void tree_index::select(const view_frustum* frustum, const tree_lod_view* lod, std::vector<render_tree_bucket>& near_runs,
                        std::vector<render_tree_bucket>& far_runs, double* near_pixels) const {
    near_runs.clear();
    far_runs.clear();
    if (near_pixels != nullptr) {
        *near_pixels = 0.0;
    }
    const bool images = lod != nullptr && lod->max_error_pixels > 0.0f;
    for (const group& g : groups_) {
        if (frustum == nullptr && !images) {
            near_runs.push_back(g.bucket);
            continue;
        }
        for (std::size_t b = g.first_block; b < g.first_block + g.block_count; ++b) {
            const block& in_block = blocks_[b];
            if (frustum != nullptr && !frustum_intersects_aabb(*frustum, in_block.bounds.min, in_block.bounds.max)) {
                continue;
            }
            for (std::size_t c = in_block.first_cell; c < in_block.first_cell + in_block.cell_count; ++c) {
                const cell& in_cell = cells_[c];
                if (frustum != nullptr && !frustum_intersects_aabb(*frustum, in_cell.bounds.min, in_cell.bounds.max)) {
                    continue;
                }
                const float distance = distance_to_box(lod != nullptr ? lod->eye : glm::vec3(0.0f), in_cell.bounds);
                const bool far = images &&
                    tree_image_error_pixels(*lod, in_cell.crown_radius, in_cell.height, distance) <= lod->max_error_pixels;
                if (!far && near_pixels != nullptr && lod != nullptr && lod->focal_pixels > 0.0f) {
                    const double radius_pixels = in_cell.crown_radius * lod->focal_pixels / std::max(distance, 1.0f);
                    *near_pixels += 3.14159265 * radius_pixels * radius_pixels * static_cast<double>(in_cell.count);
                }
                if (frustum != nullptr && frustum_hides_aabb(*frustum, in_cell.bounds.min, in_cell.bounds.max)) {
                    continue;
                }
                append_run(far ? far_runs : near_runs, far ? near_runs : far_runs, g.bucket, in_cell.first, in_cell.count);
            }
        }
    }
}
