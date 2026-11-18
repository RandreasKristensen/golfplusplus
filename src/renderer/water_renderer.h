#pragma once

// Draws the ponds (renderer/water_batch.h): their surfaces, opaque, composing
// what lies under them themselves from a copy of the scene drawn so far (seen
// through the water's depth, its light fading with it, mixed with the sky it
// reflects and the sun's glint), hazed like the terrain; and, while the
// camera is under one, the whole picture seen through the water: fogged
// along each pixel's path, caustics on what lies under the surface, wobbling.
// Uploads the surfaces only when they are rebuilt.

#include <cstddef>
#include <cstdint>
#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "game/lighting_loader.h"
#include "profiling/profiling.h"
#include "renderer/dynamic_buffer.h"
#include "renderer/framebuffer.h"
#include "renderer/gl_timer.h"
#include "renderer/shader.h"
#include "renderer/water_batch.h"
#include "renderer/water_reflection.h"

struct render_weather;  // renderer/render_data.h

class water_renderer {
public:
    // `screen_vao` is a full-screen quad (position.xyz first) owned by the caller.
    bool init(const std::string& surface_vertex, const std::string& surface_fragment, const std::string& underwater_vertex,
              const std::string& underwater_fragment, unsigned int screen_vao);
    void shutdown();
    // Exact current-frame visibility against the opaque scene depth. Queries
    // finish before a mirror is drawn, so a newly visible pond is never missed.
    std::vector<float> visible_levels(const render_water* water,const glm::mat4& view_proj,frame_profile* profile);

    // Copies the bound framebuffer's colour and depth (when there are
    // surfaces) and draws the surfaces over it; draw after everything solid.
    // Leaves that framebuffer and its viewport bound.
    // `weather` roughens them (surface_wind and surface_rain); each surface
    // samples its real mirrored scene from `reflections`.
    void draw_surfaces(const render_water* water, const glm::mat4& view_proj, const water_style& style,
                       const render_weather& weather, const std::vector<water_reflection>& reflections, float clock_seconds, gl_timer_pool& timers,
                       frame_profile* profile);
    // The bound framebuffer's picture seen from under a pond whose surface
    // is at `water_level`, redrawn over itself; draw after the scene, before
    // the HUD and what is held. Leaves that framebuffer and its viewport bound.
    void draw_underwater(const glm::mat4& view_proj, const water_style& style, float water_level, float clock_seconds,
                         gl_timer_pool& timers, frame_profile* profile);

private:
    void upload(const render_water& water,frame_profile* profile);
    bool ensure_beneath(int width, int height);
    // Copies the bound framebuffer into `beneath_`, then binds it and its
    // viewport again.
    bool copy_beneath();
    void bind_beneath() const;

    struct surface_run {
        float level = 0.0f;
        int first = 0;
        int count = 0;
    };
    std::vector<surface_run> surfaces_;
    std::vector<unsigned int> visibility_queries_;
    shader_program visibility_shader_;
    shader_program surface_shader_;
    shader_program underwater_shader_;
    unsigned int vao_ = 0;
    unsigned int screen_vao_ = 0;
    framebuffer beneath_;  // the scene before the water (or before being seen through it), which the shaders read
    dynamic_vertex_buffer buffer_;
    std::uint64_t uploaded_revision_ = 0;
    bool uploaded_ = false;
};
