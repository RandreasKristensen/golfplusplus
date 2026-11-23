#pragma once

// Draws the course's trees and bushes, each its species' model grown in code
// (renderer/tree_model.h), and the same models into the shadow maps
// (renderer/shadow_map_pass.h, lamp_shadow_pass.h). The trees' instances are
// indexed (renderer/render_tree.h) and uploaded into one static buffer once
// per course revision; each view draws the runs of it that it sees, a run at
// a time: the full model near the eye, trees far enough for their baked image
// (renderer/tree_impostor_renderer.h) as that, and the full model in every
// shadow pass; whole crowns that cover the picture many times over are drawn depth
// first so each pixel is lit once. Each species is drawn in its season on the
// course's date (render_tree_species): its leaves fresh, summer green or
// turned, as many of them as its crown has in leaf and the withered ones it
// keeps, and the fine twigs round them, so a bare crown is its branches in a
// haze of twigs; every pass draws the same cards, so the shadows' crowns drop
// their leaves with it. Near trees drop their falling leaves
// (renderer/falling_leaf.h) in the picture, zoom and mirror passes, never into
// a shadow map or a bake.
// The models are grown again only when the palette's tree looks change.

#include "renderer/gl_storage.h"
#include <cstddef>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "game/lighting_loader.h"
#include "physics/tree_body.h"
#include "profiling/profiling.h"
#include "renderer/falling_leaf_renderer.h"
#include "renderer/frustum.h"
#include "renderer/render_data.h"
#include "renderer/render_tree.h"
#include "renderer/shader.h"
#include "renderer/tree_impostor_renderer.h"

// Where a pass looks from, for choosing which trees are drawn as images:
// pixels per radian of its own projection and viewport. 0 draws every tree whole.
struct tree_eye {
    glm::vec3 position{0.0f};
    float focal_pixels = 0.0f;
    float viewport_pixels = 0.0f;  // its picture's area; 0 never draws depth first
    bool lamps = false;            // the night lights are on: every lit pixel costs more
};

class tree_renderer {
public:
    explicit tree_renderer(gl_storage storage = {})
        : storage_(storage), shader_(storage), depth_shader_(storage), leaves_(storage), images_(storage) {}

    // The tree_bake and tree_image shaders are found beside `vertex_path`.
    bool init(const std::string& vertex_path, const std::string& fragment_path, const std::vector<tree_look>& looks,
              const tree_impostor_style& style);
    void shutdown();
    // Grows the models again for new looks (a saved lighting.json).
    bool set_looks(const std::vector<tree_look>& looks, const tree_impostor_style& style);
    // How leaves fall; none until set.
    void set_falling_leaves(const falling_leaf_style& style) { leaves_.set_style(style); }
    // This frame's species and seasons, before anything is drawn. A change
    // bakes the images of their models again, in place.
    void set_seasons(const std::vector<render_tree_species>& species, frame_profile* profile = nullptr);

    // Indexes and uploads `trees` unless this revision of them is already
    // held. Call before any pass; the clock and wind stay uniforms.
    void set_trees(const std::vector<tree_body>& trees, std::uint64_t revision, frame_profile* profile);
    std::size_t tree_count() const { return index_.instances().size(); }
    std::uint64_t trees_revision() const { return trees_revision_; }

    // Set before any pass, including shadow maps.
    void set_motion(float seconds, const glm::vec2& wind) { seconds_ = seconds; wind_ = wind; }
    const glm::vec2& motion_wind() const { return wind_; }
    std::uint64_t model_revision() const { return model_revision_; }
    bool moving() const { return wind_.x != 0.0f || wind_.y != 0.0f; }
    float motion_time() const { return seconds_; }
    // The most a crown sways per m/s of wind: what tree_pose_drift needs.
    float pose_reach() const { return index_.pose_reach(); }

    // The trees `frustum` sees, lit, shadowed (bind the shadow map
    // into shader() and image_shader() first), swaying in `wind` and hazed. Returns true when
    // any was drawn.
    bool draw(const glm::mat4& view_proj, const view_frustum& frustum, float clock_seconds, const glm::vec2& wind,
              frame_profile* profile, const tree_eye& eye = tree_eye{});
    // The trees `view_proj` sees with `depth_shader` (in use, instanced like
    // tree.vert's first six floats): a shadow map's casters.
    void draw_casters(const shader_program& depth_shader, const glm::mat4& view_proj, frame_profile* profile);

    const shader_program& shader() const { return shader_; }
    const shader_program& leaf_shader() const { return leaves_.shader(); }
    const shader_program& image_shader() const { return images_.shader(); }
    const tree_impostor_renderer& images() const { return images_; }

private:
    gl_storage storage_;
    struct model_mesh {
        unsigned int vao = 0;
        unsigned int vbo = 0;
        unsigned int ebo = 0;
        int index_count = 0;  // indices of bark and cards
        tree_box box;
    };
    // A species' season as drawn: its look, the colour its leaves are now and
    // the shares of its crown in leaf and withered, and how many are falling
    // (which the baked images never show).
    struct species_season {
        std::size_t look = 0;
        float foliage = 1.0f;
        float withered = 0.0f;
        glm::vec3 leaves{0.0f};
        float falling = 0.0f;
        float falling_withered = 0.0f;
        bool operator==(const species_season& other) const {
            return look == other.look && foliage == other.foliage && withered == other.withered && leaves == other.leaves;
        }
    };

    void release_meshes();
    // The cards' cutout for `season` (tree_leaf.glsl), in `program`: every pass sets the same.
    void set_card_uniforms(const shader_program& program, const species_season& season) const;
    const model_mesh* mesh_for(std::size_t look, int variant) const;
    // Draws the selected runs, setting each bucket's seasonal uniforms first.
    template <typename Draw>
    void draw_selected(frame_profile* profile, bool counted, Draw set_bucket_uniforms);

    // Bakes every species' variants for the seasons held, when they are not baked.
    void bake_images(frame_profile* profile);
    // The selected near runs' falling leaves.
    void draw_falling_leaves(const glm::mat4& view_proj, const tree_eye& eye, float clock_seconds, const glm::vec2& wind,
                             frame_profile* profile);
    std::vector<falling_leaf_run> leaf_runs_;  // scratch

    shader_program shader_;
    shader_program depth_shader_;  // the casters' shader: the picture's depth before its lighting
    falling_leaf_renderer leaves_;
    render_tree_instance largest_tree_;  // the most leaves any tree drops
    tree_impostor_renderer images_;
    std::string shader_directory_;
    std::vector<species_season> baked_seasons_;
    std::uint64_t baked_models_ = 0;
    std::vector<tree_image_run> image_runs_;  // scratch
    std::vector<tree_look> looks_;
    std::vector<model_mesh> meshes_;  // look by variant
    std::vector<species_season> seasons_;
    tree_index index_;
    std::vector<render_tree_bucket> selected_;  // scratch, one view at a time
    std::vector<render_tree_bucket> far_selected_;
    unsigned int instance_vbo_ = 0;
    std::uint64_t trees_revision_ = 0;
    std::uint64_t model_revision_ = 0;
    float seconds_ = 0.0f;
    glm::vec2 wind_{0.0f};
};
