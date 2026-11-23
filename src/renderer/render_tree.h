#pragma once

// GL-free: the trees as the instanced draws take them (unit tested). Each
// tree is one instance of its species' grown model (renderer/tree_model.h):
// the model's unit frame stood at the trunk's foot, scaled to the crown's
// radius across and the tree's height up, turned about its trunk, with a
// tint to tell neighbours apart. tree_index builds every instance once per
// course revision, sorted by species and variant and then by place, with
// bounds that hold at the strongest wind; a view then asks it for the runs
// of that array it sees, those near enough to need the full model and those
// far enough (tree_image_error_pixels) for its baked image.

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

// A run of one species' and variant's instances in tree_index::instances().
struct render_tree_bucket {
    std::size_t species = 0;
    int variant = 0;
    std::size_t first = 0;
    std::size_t count = 0;
};

// Where a tree can put any of its leaves or bark, wind included.
struct tree_bounds {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

// How a pass looks at the trees: where its eye is and how many pixels a
// radian covers in its picture (its own projection and viewport), so a zoomed
// or mirrored pass judges its own error. max_error_pixels 0 means no images.
struct tree_lod_view {
    glm::vec3 eye{0.0f};
    float focal_pixels = 0.0f;
    float max_error_pixels = 0.0f;
    float radius_error = 0.0f;  // tree_impostor_style
    float wind_error = 0.0f;
    float wind_speed = 0.0f;    // m/s
};

// The most pixels an image of a tree of `crown_radius` and `height` seen
// from `distance` can differ from its model by: infinite with no lens or no distance.
float tree_image_error_pixels(const tree_lod_view& view, float crown_radius, float height, float distance);

render_tree_instance tree_instance_for(const tree_body& tree);

// How far, per m/s of wind, the top of a tree's crown (and its leaf cards) can
// sway at most (tree_pose.glsl at full gust).
float tree_pose_reach(const render_tree_instance& instance);

// Which of its species' models a tree is, fixed by where it stands.
int tree_variant_for(const tree_body& tree);

// The box an instance stays within at any clock and any wind a look may set
// (tree_pose.glsl bends the crown with the wind and the height squared).
tree_bounds tree_bounds_for(const render_tree_instance& instance);

class tree_index {
public:
    // Replaces the index with `trees`' instances (tree_instance_for) in
    // species, variant, then place order, each place a cell of a block.
    void build(const std::vector<tree_body>& trees);
    const std::vector<render_tree_instance>& instances() const { return instances_; }
    bool empty() const { return instances_.empty(); }
    // The most any of its trees sways per m/s of wind (tree_pose_reach).
    float pose_reach() const { return pose_reach_; }
    // Replaces `near_runs` and `far_runs` with the runs of instances whose
    // cells `frustum` sees (all when null), in bucket order: a cell goes to
    // `far_runs` only when even its nearest tree is within `lod`'s error
    // (never when `lod` is null). Every tree it sees is in one run of one of
    // them; runs may hold neighbours that are not, but never one of the
    // other list's.
    // With `lod`, `near_pixels` (when given) is set to the picture area the near
    // crowns would cover, overlapping ones counted each: how much a lit leaf
    // would be shaded over.
    void select(const view_frustum* frustum, const tree_lod_view* lod, std::vector<render_tree_bucket>& near_runs,
                std::vector<render_tree_bucket>& far_runs, double* near_pixels = nullptr) const;

private:
    struct cell {
        tree_bounds bounds;
        std::size_t first = 0;
        std::size_t count = 0;
        float crown_radius = 0.0f;  // the largest in the cell
        float height = 0.0f;
    };
    struct block {
        tree_bounds bounds;
        std::size_t first_cell = 0;
        std::size_t cell_count = 0;
    };
    struct group {
        render_tree_bucket bucket;
        std::size_t first_block = 0;
        std::size_t block_count = 0;
    };

    std::vector<render_tree_instance> instances_;
    std::vector<group> groups_;
    std::vector<block> blocks_;
    std::vector<cell> cells_;
    float pose_reach_ = 0.0f;
};

// A unit-frame point of a tree as it stands, as tree.vert places it.
glm::vec3 tree_world_point(const render_tree_instance& instance, const glm::vec3& local);
