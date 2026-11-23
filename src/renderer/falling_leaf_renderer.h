#pragma once

// Draws near trees' falling leaves (renderer/falling_leaf.h, replayed by
// falling_leaf.vert): one instanced draw per species run of the trees'
// static instance buffer (renderer/render_tree.h), six vertices a leaf slot,
// nothing uploaded or kept per leaf. As many slots a tree as the tree dropping
// the most needs, within the style's caps a tree and a pass.

#include "renderer/gl_storage.h"

#include <cstddef>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "game/lighting_loader.h"
#include "profiling/profiling.h"
#include "renderer/render_tree.h"
#include "renderer/shader.h"
#include "renderer/tree_skeleton.h"

// A run of one species' trees and how its leaves fall today.
struct falling_leaf_run {
    std::size_t first = 0;
    std::size_t count = 0;
    tree_box crown;           // tree_crown_box of its look
    float daily_share = 0.0f; // leaves_falling
    float withered = 0.0f;    // the share of them withered
    glm::vec3 leaf_color{0.0f};
    glm::vec3 withered_color{0.0f};
};

class falling_leaf_renderer {
public:
    explicit falling_leaf_renderer(gl_storage storage = {}) : shader_(storage) {}

    bool init(const std::string& vertex_path, const std::string& fragment_path);
    void shutdown();
    void set_style(const falling_leaf_style& style) { style_ = style; }
    // Bind the shadow map into it before drawing.
    const shader_program& shader() const { return shader_; }

    // `runs` of `instance_buffer` seen from `eye` with `focal_pixels` to the
    // radian; `largest` the tree whose crown drops the most; `trees` how many
    // trees the runs hold. Nothing without a lens.
    void draw(const std::vector<falling_leaf_run>& runs, unsigned instance_buffer, const render_tree_instance& largest,
              std::size_t trees, const glm::mat4& view_proj, const glm::vec3& eye, float focal_pixels, float seconds,
              const glm::vec2& wind, frame_profile* profile);

private:
    shader_program shader_;
    falling_leaf_style style_;
    unsigned int vao_ = 0;
};
