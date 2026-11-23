#pragma once

// The shadow map: while the sun is up, the instanced tree batch, the
// floodlights' mesh and the figures, carts, flagsticks, signs and fence posts
// (renderer/shadow_casters.h; the models and figures are instances in the
// buffers the prop and marker passes already hold, drawn instanced by
// shadow_map.vert's MODEL_CASTER, the signs and posts a static buffer of
// their own, selected by tile) drawn, when anything that shows has changed (draw), into a small depth texture looking down the sun over the
// ground around the camera (shadow_map_fit.h), which terrain.frag and
// tee_box.frag sample in a few soft taps (shadow_map_light.glsl) and multiply
// into the sun the land lets through (lighting.glsl's sun_shadow). Chunky and
// soft on purpose: a camcorder never saw a crisp shadow edge. With the sun
// down the pass is skipped and the ground sees no cast shadow.

#include "renderer/gl_storage.h"
#include "game/lighting_loader.h"
#include "physics/tree_body.h"
#include "profiling/profiling.h"
#include "renderer/caster_motion.h"
#include "renderer/dynamic_buffer.h"
#include "renderer/floodlight_renderer.h"
#include "renderer/gl_timer.h"
#include "renderer/caster_index.h"
#include "renderer/instance_meshes.h"
#include "renderer/mesh_instance.h"
#include "renderer/prop_frame.h"
#include "renderer/shader.h"
#include "renderer/tree_pose_drift.h"
#include "renderer/tree_renderer.h"
#include "renderer/world_marker_batch.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

// Where the models that cast live: the prop pass's static buffer (the placed
// models' opaque instances of each mesh) and frame buffer (these runs of this
// frame's moving models), both of mesh_instance.
struct prop_caster_source {
    unsigned int static_buffer = 0;
    std::array<prop_range, prop_mesh_count> static_runs{};
    unsigned int frame_buffer = 0;
    prop_ranges frame{};
    // Changes with the static models; `frame_batch` is the frame's instances on the CPU, which
    // also say where each list starts in the frame buffer.
    std::uint64_t static_revision = 0;
    const prop_batch* frame_batch = nullptr;
};

// The figures and flagsticks that cast: the runs of `batch` (what the world
// markers uploaded) that the sun's shadow shows, in the buffers holding them:
// its triangles in `buffer`, its instances in `instance_buffer`.
struct marker_caster_source {
    unsigned int buffer = 0;
    unsigned int instance_buffer = 0;
    const world_marker_batch* batch = nullptr;
};

class shadow_map_pass {
public:
    explicit shadow_map_pass(gl_storage storage = {})
        : storage_(storage),
          shader_(storage),
          tree_shader_(storage), model_shader_(storage), meshes_(storage), static_buffer_(storage) {}

    bool init(const std::string& vertex_path, const std::string& fragment_path, const shadow_style& style);
    void shutdown();
    // Uploads the static casters (sign and fence triangle corners) (replacing any held).
    void set_static_casters(const std::vector<glm::vec3>& corners, frame_profile* profile);
    bool static_casters_held() const { return static_held_; }
    // This frame's map of `tree_batch`'s trees, `floodlights`, `figures` and `models`, for the sun `towards_sun` casting
    // `sun_share` (0..1, render_shadows) of its shadow, centred on `eye`.
    // The map is drawn again only when it can look different: the sun's fit, the trees, floodlights, static
    // casters changed, the sun turned more than map_sun_degrees, the camera left its texel, or a crown or a moving caster
    // has moved more than map_pose_texels of a map texel from where it was drawn.
    // Leaves its framebuffer bound when it draws: the caller binds its own.
    void draw(tree_renderer& tree_batch, floodlight_renderer& floodlight_batch, const render_floodlights* floodlights,
              const marker_caster_source& figures, const prop_caster_source& models, const glm::vec3& towards_sun, float sun_share,
              const glm::vec3& eye,
              gl_timer_pool& timers, frame_profile* profile);
    // Binds the map to `unit` and tells `receiver` (any shader including
    // shadow_map_light.glsl, in use) which unit it is on and how to read it; with no map this frame it shades nothing.
    void bind(const shader_program& receiver, int unit) const;
    // How much of the sun this frame's map takes away where it shades, 0
    // with no map.
    float strength() const { return strength_; }
    const shadow_style& style() const { return style_; }
    // How far a crown may sway before a map of it is stale, in metres.
    float pose_tolerance_meters() const { return style_.map_extent / static_cast<float>(style_.map_resolution) * style_.map_pose_texels; }

private:
    // What the held map was drawn from.
    struct drawn_map {
        bool valid = false;
        glm::vec3 towards_sun{0.0f, 1.0f, 0.0f};
        glm::vec2 corner{0.0f};
        float depth_middle = 0.0f;
        std::uint64_t trees = 0;
        std::uint64_t tree_models = 0;
        std::uint64_t floodlights = 0;
        std::uint64_t static_casters = 0;
        std::uint64_t static_models = 0;
        caster_motion moving;
        tree_pose_drift pose;
    };
    // Calls `visit` with every position the map's moving casters are tracked by: the vertices of the figures'
    // triangles, and the corners of every instance's mesh (placed_corners), the models' and the figures'.
    template <typename visitor>
    static void each_moving(const marker_caster_source& figures, const prop_caster_source& models, const visitor& visit) {
        if (models.frame_batch != nullptr) {
            for (const prop_mesh mesh : {prop_mesh::cylinder, prop_mesh::sphere, prop_mesh::quad}) {
                const std::vector<mesh_instance>& list = models.frame_batch->instances(false, mesh);
                const prop_runs& runs = models.frame[static_cast<std::size_t>(mesh)];
                for (int i = 0; i < runs.count; ++i) {
                    const prop_range& run = runs.run[static_cast<std::size_t>(i)];
                    for (std::size_t n = run.first; n < run.first + run.count; ++n) {
                        for (const glm::vec3& corner : placed_corners(list[n], mesh)) { visit(corner); }
                    }
                }
            }
        }
        if (figures.batch == nullptr) {
            return;
        }
        for (const world_marker_run& run : figures.batch->runs()) {
            if (figures.batch->shown_in(run, marker_view::sun_shadow)) {
                for (std::size_t v = run.first; v < run.first + run.count; ++v) {
                    visit(figures.batch->vertices()[v].position);
                }
            }
        }
        for (const world_marker_instance_run& run : figures.batch->instance_runs()) {
            if (figures.batch->shown_in(run.part, marker_view::sun_shadow)) {
                const std::vector<mesh_instance>& list = figures.batch->instances(run.mesh);
                for (std::size_t n = run.first; n < run.first + run.count; ++n) {
                    for (const glm::vec3& corner : placed_corners(list[n], run.mesh)) { visit(corner); }
                }
            }
        }
    }
    gl_storage storage_;
    void draw_figures(const marker_caster_source& figures, frame_profile* profile);
    void draw_vertices(unsigned int buffer, std::size_t first, std::size_t count, frame_profile* profile);
    static bool has_instances(const prop_caster_source& models);
    void draw_instances(const marker_caster_source& figures, const prop_caster_source& models, frame_profile* profile);
    void draw_static_casters(const glm::vec3& eye, frame_profile* profile);

    shader_program shader_;
    shader_program tree_shader_;
    shader_program model_shader_;  // MODEL_CASTER: the figures' and models' instances
    instance_meshes meshes_;
    unsigned int models_vao_ = 0;
    unsigned int static_vao_ = 0;
    dynamic_vertex_buffer static_buffer_;
    caster_index static_index_;
    bool static_held_ = false;
    std::uint64_t static_epoch_ = 0;
    drawn_map drawn_;
    shadow_style style_;
    unsigned int fbo_ = 0;
    unsigned int depth_texture_ = 0;
    glm::mat4 view_proj_{1.0f};
    float strength_ = 0.0f;
    float depth_bias_ = 0.0f;  // in the map's depth units
};
