#pragma once

// Draws the course's trees and bushes, each its species' model grown in code
// (renderer/tree_model.h), and the same models into the shadow maps
// (renderer/shadow_map_pass.h, lamp_shadow_pass.h). Every frame the trees in
// view are sorted into one bucket per species and variant
// (renderer/render_tree.h), uploaded into one instance buffer and drawn a
// bucket at a time, using the full model in every pass. Each species is drawn in its season on the
// course's date (render_tree_species): its leaves fresh, summer green or
// turned, and as many of them as its crown has in leaf, so a bare crown
// shows its branches; the shadows' crowns drop their leaves with it.
// The models are grown again only when the palette's tree looks change.

#include <cstddef>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "game/lighting_loader.h"
#include "profiling/profiling.h"
#include "renderer/dynamic_buffer.h"
#include "renderer/frustum.h"
#include "renderer/render_data.h"
#include "renderer/render_tree.h"
#include "renderer/shader.h"

class tree_renderer {
public:
    bool init(const std::string& vertex_path, const std::string& fragment_path, const std::vector<tree_look>& looks);
    void shutdown();
    // Grows the models again for new looks (a saved lighting.json).
    bool set_looks(const std::vector<tree_look>& looks);
    // This frame's species and seasons, before anything is drawn.
    void set_seasons(const std::vector<render_tree_species>& species);

    // Set before any pass, including shadow maps.
    void set_motion(float seconds, const glm::vec2& wind) { seconds_ = seconds; wind_ = wind; }
    const glm::vec2& motion_wind() const { return wind_; }
    std::uint64_t model_revision() const { return model_revision_; }
    bool moving() const { return wind_.x != 0.0f || wind_.y != 0.0f; }
    float motion_time() const { return seconds_; }

    // The trees `frustum` sees, lit, shadowed (bind the shadow map
    // into shader() first), swaying in `wind` and hazed. Returns true when
    // any was drawn.
    bool draw(const std::vector<tree_body>& trees, const glm::mat4& view_proj, const view_frustum& frustum,
              float clock_seconds, const glm::vec2& wind, frame_profile* profile);
    // The trees `view_proj` sees with `depth_shader` (in use, instanced like
    // tree.vert's first six floats): a shadow map's casters.
    void draw_casters(const std::vector<tree_body>& trees, std::uint64_t revision, const shader_program& depth_shader,
                      const glm::mat4& view_proj, frame_profile* profile);

    const shader_program& shader() const { return shader_; }

private:
    struct model_mesh {
        unsigned int vao = 0;
        unsigned int vbo = 0;
        int bark_count = 0;
        int vertex_count = 0;
    };
    // A species' season as drawn: its look and the colour its leaves are now.
    struct species_season {
        std::size_t look = 0;
        float foliage = 1.0f;
        glm::vec3 leaves{0.0f};
    };

    void release_meshes();
    const model_mesh* mesh_for(std::size_t look, int variant) const;
    // Uploads `batch`'s instances, then draws each bucket with `draw_bucket`.
    template <typename Draw>
    void draw_batch(const render_tree_batch& batch, frame_profile* profile, Draw draw_bucket);

    shader_program shader_;
    std::vector<tree_look> looks_;
    std::vector<model_mesh> meshes_;  // look by variant
    std::vector<species_season> seasons_;
    dynamic_vertex_buffer instances_;
    std::uint64_t model_revision_ = 0;
    float seconds_ = 0.0f;
    glm::vec2 wind_{0.0f};
};
