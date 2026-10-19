#pragma once

// The trees' shadow map: while the sun is up, the instanced tree batch and
// the floodlights' mesh drawn once a frame into a small depth texture looking down the sun over the
// ground around the camera (shadow_map_fit.h), which terrain.frag samples in
// a few soft taps and multiplies into the sun the land lets through
// (lighting.glsl's sun_shadow). Chunky and soft on purpose: a camcorder never
// saw a crisp shadow edge. With the sun down the pass is skipped and the
// ground sees no tree shadow.

#include "game/lighting_loader.h"
#include "physics/tree_collision.h"
#include "profiling/profiling.h"
#include "renderer/floodlight_renderer.h"
#include "renderer/gl_timer.h"
#include "renderer/shader.h"
#include "renderer/tree_renderer.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class shadow_map_pass {
public:
    bool init(const std::string& vertex_path, const std::string& fragment_path, const shadow_style& style);
    void shutdown();
    // This frame's map of `trees` and `floodlights`, for the sun `towards_sun` casting
    // `sun_share` (0..1, render_shadows) of its shadow, around `eye` looking
    // along `forward`. Leaves its framebuffer bound: the caller binds its own.
    void draw(tree_renderer& tree_batch, const std::vector<tree_body>& trees, std::uint64_t trees_revision,
              floodlight_renderer& floodlight_batch, const render_floodlights* floodlights,
              const glm::vec3& towards_sun, float sun_share, const glm::vec3& eye, const glm::vec3& forward,
              gl_timer_pool& timers, frame_profile* profile);
    // Binds the map to `unit` and tells `terrain` (the terrain shader, in
    // use) how to read it; with no map this frame it shades nothing.
    void bind(const shader_program& terrain, int unit) const;

private:
    shader_program shader_;
    shadow_style style_;
    unsigned int fbo_ = 0;
    unsigned int depth_texture_ = 0;
    glm::mat4 view_proj_{1.0f};
    float strength_ = 0.0f;
    float depth_bias_ = 0.0f;  // in the map's depth units
};
