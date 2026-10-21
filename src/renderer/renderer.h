#pragma once

// Draws a render_data frame: the 3D scene into a low-resolution HDR
// framebuffer, the camcorder that records it, the tape pass, the overlay on
// top of it, then the TV pass to the window (all always on). A still frame
// keeps the last recording and plays the tape over it.

#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>

#include "game/lighting_loader.h"
#include "game/picture_loader.h"
#include "game/text_assets.h"
#include "profiling/profiling.h"
#include "renderer/backdrop_pass.h"
#include "renderer/blob_shadow_renderer.h"
#include "renderer/camcorder_pass.h"
#include "renderer/course_map_fill.h"
#include "renderer/dynamic_buffer.h"
#include "renderer/emote_props.h"
#include "renderer/framebuffer.h"
#include "renderer/gl_timer.h"
#include "renderer/fence_renderer.h"
#include "renderer/floodlight_renderer.h"
#include "renderer/hole_sign_renderer.h"
#include "renderer/lighting_uniforms.h"
#include "renderer/water_renderer.h"
#include "renderer/weather_renderer.h"
#include "renderer/overlay_pass.h"
#include "renderer/picture_size.h"
#include "renderer/render_data.h"
#include "renderer/shader.h"
#include "renderer/shadow_map_pass.h"
#include "renderer/eyepiece_pass.h"
#include "renderer/tape_pass.h"
#include "renderer/terrain_renderer.h"
#include "renderer/texture.h"
#include "renderer/tree_renderer.h"
#include "renderer/tv_pass.h"
#include "renderer/world_marker_batch.h"
#include "renderer/world_marker_renderer.h"

// Frustum culling result for the static scene meshes, for the profiler.
struct renderer_cull_stats {
    render_chunk_cull_stats terrain;
    render_chunk_cull_stats material_overlay;
    bool trees_visible = false;  // false when the whole tree batch was culled
};

class renderer {
public:
    // `asset_root` is where shaders/, textures/ and course backdrops are read
    // from; `palette` says what the scene's things are, what the sky shows
    // and how the shadow map is drawn (lighting.json).
    bool init(SDL_Window* window, const std::string& asset_root, const picture_style& picture,
              const lighting_palette& palette);
    void shutdown();
    // `profile` may be null (profiling off).
    void render(const render_data& data, const text_assets& text, frame_profile* profile = nullptr);
    // The loading title card, drawn before any content is loaded (no scene).
    void render_loading_screen(const text_assets& text);
    // The tape wear setting, 0 fresh .. 1 worn.
    void set_tape_wear(float wear) { tape_pass_.set_wear(wear); }
    const renderer_cull_stats& cull_stats() const { return cull_stats_; }

private:
    // A non-indexed position+normal mesh (unit primitives).
    struct primitive_buffers {
        unsigned int vao = 0;
        unsigned int vbo = 0;
        int vertex_count = 0;
    };

    bool init_geometry();
    bool ensure_framebuffer_size(int screen_width, int screen_height);
    // The low-res target's perspective at `fov_degrees`, far enough for the area.
    glm::mat4 scene_projection(float fov_degrees, float area_extent) const;
    void render_scene(const glm::mat4& view, const glm::mat4& proj, const render_data& data, frame_profile* profile);
    void render_viewmodel(const glm::mat4& view, const render_data& data);
    void draw_emote_props(const glm::mat4& view_proj);
    void render_flight_path(const glm::mat4& view, const glm::mat4& proj, const render_data& data, frame_profile* profile);
    void render_overlay(const glm::mat4& view_proj, const render_data& data, const text_assets& text, frame_profile* profile);
    void render_debug_overlay(const render_data& data, const text_assets& text, frame_profile* profile);
    void render_course_map(const render_data& data, const text_assets& text);
    // `picture` through the tape pass, leaving the tape target bound for the overlay.
    void render_tape(unsigned int picture, float seconds, float tracking);

    SDL_Window* window_ = nullptr;
    overlay_pass overlay_pass_;
    backdrop_pass backdrop_pass_;
    texture grass_texture_;  // on the rough (terrain.frag)
    framebuffer scene_fbo_;
    camcorder_pass camcorder_pass_;
    eyepiece_pass eyepiece_pass_;
    tape_pass tape_pass_;
    tv_pass tv_pass_;
    gl_timer_pool gpu_timers_;
    lighting_uniforms lighting_uniforms_;
    shader_program terrain_shader_;
    shader_program ball_shader_;
    tree_renderer tree_renderer_;
    hole_sign_renderer hole_sign_renderer_;
    fence_renderer fence_renderer_;
    floodlight_renderer floodlight_renderer_;
    water_renderer water_renderer_;
    weather_renderer weather_renderer_;
    world_marker_renderer world_marker_renderer_;
    world_marker_renderer tee_box_renderer_;
    blob_shadow_renderer blob_shadow_renderer_;
    shadow_map_pass shadow_map_pass_;
    // Rebuilt every frame; members so their vectors keep their capacity.
    world_marker_batch world_marker_batch_;
    world_marker_batch shadow_casters_;
    world_marker_batch tee_box_batch_;
    std::vector<emote_prop> emote_props_;  // this frame's, kept for their capacity
    std::vector<render_terrain_vertex> flight_path_vertices_;
    // Course map terrain fill: rebuilt only when the map layout or terrain changes.
    course_map_fill_cache course_map_fill_cache_;
    dynamic_vertex_buffer flight_path_buffer_;
    unsigned int flight_path_vao_ = 0;
    renderer_cull_stats cull_stats_;

    terrain_renderer terrain_renderer_;
    primitive_buffers ground_;     // unit quad backdrop under the course
    primitive_buffers ball_;       // unit sphere
    primitive_buffers cylinder_;   // unit cylinder, shared with tree_renderer
    primitive_buffers cone_;       // unit cone, only drawn by tree_renderer
    primitive_buffers panel_;      // unit quad in XY, for emote props
    unsigned int screen_vao_ = 0;  // full-screen quad for the screen passes
    unsigned int screen_vbo_ = 0;

    picture_style picture_style_;
    scene_colors colors_;  // its glow floors this frame's (render_data::glow)
    picture_size picture_;  // the scene target's, for the window's current size
};
