#pragma once

// Draws a render_data frame: the 3D scene into a low-resolution framebuffer,
// the overlay on top, then the CRT pass to the window (always on).

#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>

#include "game/text_assets.h"
#include "profiling/profiling.h"
#include "renderer/course_map_fill.h"
#include "renderer/dynamic_buffer.h"
#include "renderer/framebuffer.h"
#include "renderer/gl_timer.h"
#include "renderer/overlay_pass.h"
#include "renderer/render_data.h"
#include "renderer/render_mesh_chunks.h"
#include "renderer/shader.h"
#include "renderer/tree_renderer.h"
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
    // `asset_root` is where shaders/ is read from.
    bool init(SDL_Window* window, const std::string& asset_root);
    void shutdown();
    // `profile` may be null (profiling off).
    void render(const render_data& data, const text_assets& text, frame_profile* profile = nullptr);
    const renderer_cull_stats& cull_stats() const { return cull_stats_; }

private:
    // One uploaded render_static_mesh: VAO + vertex and index buffers,
    // re-uploaded only when the mesh revision changes.
    struct static_mesh_buffers {
        unsigned int vao = 0;
        unsigned int vbo = 0;
        unsigned int ebo = 0;
        int index_count = 0;
        std::uint64_t uploaded_revision = 0;
        bool uploaded = false;
    };
    // A non-indexed position+normal mesh (unit primitives).
    struct primitive_buffers {
        unsigned int vao = 0;
        unsigned int vbo = 0;
        int vertex_count = 0;
    };

    bool init_geometry();
    bool ensure_framebuffer_size(int screen_width, int screen_height);
    void upload_static_mesh(static_mesh_buffers& buffers, const render_static_mesh* mesh, frame_profile* profile);
    void render_scene(const glm::mat4& view, const glm::mat4& proj, const render_data& data, frame_profile* profile);
    void render_emotes(const glm::mat4& view, const glm::mat4& proj, const render_data& data);
    void render_flight_path(const glm::mat4& view, const glm::mat4& proj, const render_data& data, frame_profile* profile);
    void render_overlay(const glm::mat4& view_proj, const render_data& data, const text_assets& text, frame_profile* profile);
    void render_debug_overlay(const render_data& data, const text_assets& text, frame_profile* profile);
    void render_course_map(const render_data& data);
    void render_crt(int screen_width, int screen_height);

    SDL_Window* window_ = nullptr;
    overlay_pass overlay_pass_;
    framebuffer scene_fbo_;
    gl_timer_pool gpu_timers_;
    shader_program terrain_shader_;
    shader_program ball_shader_;
    shader_program crt_shader_;
    tree_renderer tree_renderer_;
    world_marker_renderer world_marker_renderer_;
    // Rebuilt every frame; members so their vectors keep their capacity.
    world_marker_batch world_marker_batch_;
    std::vector<render_terrain_vertex> flight_path_vertices_;
    std::vector<render_index_range> terrain_draw_ranges_;
    std::vector<render_index_range> material_overlay_draw_ranges_;
    // Course map terrain fill: rebuilt only when the map layout or terrain changes.
    course_map_fill_cache course_map_fill_cache_;
    dynamic_vertex_buffer flight_path_buffer_;
    unsigned int flight_path_vao_ = 0;
    renderer_cull_stats cull_stats_;

    static_mesh_buffers terrain_;
    static_mesh_buffers material_overlay_;
    primitive_buffers ground_;     // unit quad backdrop under the course
    primitive_buffers ball_;       // unit sphere
    primitive_buffers cylinder_;   // unit cylinder, shared with tree_renderer
    primitive_buffers cone_;       // unit cone, only drawn by tree_renderer
    primitive_buffers panel_;      // unit quad in XY, for emote props
    unsigned int screen_vao_ = 0;  // full-screen quad for the CRT pass
    unsigned int screen_vbo_ = 0;

    int target_width_ = 0;
    int target_height_ = 0;
};
