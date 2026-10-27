#pragma once

// The shadow map: while the sun is up, the instanced tree batch, the
// floodlights' mesh and the figures and carts (renderer/shadow_casters.h)
// drawn once a frame into a small depth texture looking down the sun over the
// ground around the camera (shadow_map_fit.h), which terrain.frag and
// tee_box.frag sample in a few soft taps (shadow_map_light.glsl) and multiply
// into the sun the land lets through (lighting.glsl's sun_shadow). Chunky and
// soft on purpose: a camcorder never saw a crisp shadow edge. With the sun
// down the pass is skipped and the ground sees no cast shadow.

#include "game/lighting_loader.h"
#include "physics/tree_body.h"
#include "profiling/profiling.h"
#include "renderer/dynamic_buffer.h"
#include "renderer/floodlight_renderer.h"
#include "renderer/gl_timer.h"
#include "renderer/prop_batch.h"
#include "renderer/shader.h"
#include "renderer/tree_renderer.h"
#include "renderer/world_marker_batch.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class shadow_map_pass {
public:
    bool init(const std::string& vertex_path, const std::string& fragment_path, const shadow_style& style);
    void shutdown();
    // This frame's map of `trees`, `floodlights`, `figures` and `models`, for the sun `towards_sun` casting
    // `sun_share` (0..1, render_shadows) of its shadow, around `eye` looking
    // along `forward`. Leaves its framebuffer bound: the caller binds its own.
    void draw(tree_renderer& tree_batch, const std::vector<tree_body>& trees, std::uint64_t trees_revision,
              floodlight_renderer& floodlight_batch, const render_floodlights* floodlights,
              const world_marker_batch& figures, const prop_batch& models, const glm::vec3& towards_sun, float sun_share,
              const glm::vec3& eye, const glm::vec3& forward,
              gl_timer_pool& timers, frame_profile* profile);
    // Binds the map to `unit` and tells `receiver` (the terrain or tee box
    // shader, in use) how to read it; with no map this frame it shades nothing.
    void bind(const shader_program& receiver, int unit) const;

private:
    void draw_figures(const world_marker_batch& figures, frame_profile* profile);
    void draw_models(const prop_batch& models, frame_profile* profile);

    shader_program shader_;
    unsigned int figures_vao_ = 0;
    dynamic_vertex_buffer figures_buffer_;
    unsigned int models_vao_ = 0;
    dynamic_vertex_buffer models_buffer_;
    shadow_style style_;
    unsigned int fbo_ = 0;
    unsigned int depth_texture_ = 0;
    glm::mat4 view_proj_{1.0f};
    float strength_ = 0.0f;
    float depth_bias_ = 0.0f;  // in the map's depth units
};
