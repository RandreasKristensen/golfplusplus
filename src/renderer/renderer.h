#pragma once

// Draws a render_data frame: the 3D scene into a low-resolution HDR
// framebuffer, the camcorder that records it, the tape pass, the overlay on
// top of it, then the TV pass to the window (all always on). A still frame
// keeps the last recording and plays the tape over it. Owns GL storage accounting and delayed frame timings.

#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>

#include "game/lighting_loader.h"
#include "game/look_reload.h"
#include "game/prop_loader.h"
#include "game/picture_loader.h"
#include "game/text_assets.h"
#include "profiling/profiling.h"
#include "renderer/backdrop_pass.h"
#include "renderer/blob_shadow_renderer.h"
#include "renderer/camcorder_pass.h"
#include "renderer/course_map_renderer.h"
#include "renderer/draw_target.h"
#include "renderer/dynamic_buffer.h"
#include "renderer/framebuffer.h"
#include "renderer/gl_timer.h"
#include "renderer/gl_storage.h"
#include "renderer/fence_renderer.h"
#include "renderer/floodlight_renderer.h"
#include "renderer/lamp_shadow_pass.h"
#include "renderer/hole_sign_renderer.h"
#include "renderer/lighting_uniforms.h"
#include "renderer/water_renderer.h"
#include "renderer/reflection_pass.h"
#include "renderer/weather_renderer.h"
#include "renderer/overlay_pass.h"
#include "renderer/picture_size.h"
#include "renderer/prop_batch.h"
#include "renderer/prop_frame.h"
#include "renderer/prop_renderer.h"
#include "renderer/render_data.h"
#include "renderer/shader.h"
#include "renderer/shadow_map_pass.h"
#include "renderer/sun_shaft_pass.h"
#include "renderer/eyepiece_pass.h"
#include "renderer/tape_pass.h"
#include "renderer/grass_renderer.h"
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
    // Texture units of the sun's and the lamps' shadow maps.
    static constexpr int shadow_map_unit = 4;
    static constexpr int lamp_map_unit = 5;
    // `asset_root` is where shaders/, textures/ and course backdrops are read
    // from; `palette` says what the scene's things are, what the sky shows
    // and how the shadow map is drawn (lighting.json); `props` are every
    // model it draws.
    bool init(SDL_Window* window, const std::string& asset_root, const picture_style& picture,
              const lighting_palette& palette,
              const prop_catalog& props);
    void shutdown();
    // A reloaded look (game/look_reload.h), `asset_root` as init's: every
    // pass a changed section feeds is made again; the rest keep theirs.
    bool set_look(const std::string& asset_root, const look& next, const look_changes& changes);
    void render(const render_data& data, const text_assets& text);
    // Bracket update through present. The sink receives each originating record once.
    void begin_profile(frame_profile* frame) {
        allocations_.frame = frame;
        cull_stats_ = {};
    }
    std::uint64_t grass_gpu_bytes() const { return grass_texture_.logical_bytes() + grass_renderer_.logical_bytes(); }
    std::size_t grass_draw_scratch_bound() const { return grass_renderer_.scratch_bound(); }
    int grass_tile_capacity() const { return grass_renderer_.tiles().capacity(); }
    std::uint64_t grass_decoded_bytes() const { return grass_renderer_.tiles().decoded_bytes(); }
    const grass_renderer& grass() const { return grass_renderer_; }
    bool limit_grass_tiles(const int layers) { return grass_renderer_.limit_tile_layers(layers); }
    // The scene before the camcorder, for tooling that compares it with the final picture.
    const framebuffer& scene_target() const { return scene_fbo_; }
    const framebuffer& eyepiece_target() const { return eyepiece_pass_.picture(); }
    // The depth-rejected parts of the eyepiece's views are on unless a check turns them off to compare.
    void mask_eyepiece(const bool on) { eyepiece_masking_ = on; }
    void finish_profile(const frame_profile_sink& sink);
    void flush_profiles(const frame_profile_sink& sink) { gpu_timers_.flush(sink); }
    // The loading title card, drawn before any content is loaded (no scene).
    void render_loading_screen(const text_assets& text);
    // The scene resolution setting: which of the picture's `scene_lines`; the
    // targets follow on the next frame.
    void set_scene_resolution(const int preset) { scene_preset_ = preset; }
    // The scene target's size and its layout on the window, as of the last frame.
    const picture_size& picture() const { return picture_; }
    // The tape wear setting, 0 fresh .. 1 worn.
    void set_tape_wear(float wear) { tape_pass_.set_wear(wear); }

private:
    // A non-indexed position+normal mesh (unit primitives).
    struct primitive_buffers {
        unsigned int vao = 0;
        unsigned int vbo = 0;
        int vertex_count = 0;
    };

    static std::string shader_path(const std::string& asset_root, const char* name);

    bool init_geometry();
    bool ensure_framebuffer_size(int screen_width, int screen_height);
    // The low-res target's perspective at `fov_degrees`, far enough for the area.
    glm::mat4 scene_projection(float fov_degrees, float area_extent) const;
    void render_scene(const glm::mat4& view, const glm::mat4& proj, const render_data& data, const draw_target& target,
                      frame_profile* profile, bool reflected = false, const glm::mat4* sky_projection = nullptr, float reflection_level = 0.0f,
                      bool zoomed = false, const hidden_disc& hidden = hidden_disc{});
    // Whether any pond's surface may show in a view: its pixels look up what lies beside them (water.frag).
    bool water_may_show(const render_data& data, const glm::mat4& view_proj) const;
    void render_viewmodel(const glm::mat4& view, const render_data& data, frame_profile* profile);
    // The placed models and the sign and fence casters, uploaded once per course; true on a new course.
    bool refresh_static_geometry(const render_data& data, frame_profile* profile);
    // The frame's models and markers, built and uploaded once for every view.
    void stage_frame_geometry(const render_data& data, frame_profile* profile);
    // The models of this view from the uploads (a mirrored view differs from the main one in mine).
    void draw_scene_props(bool reflected, const glm::mat4& view_proj, frame_profile* profile);
    void advance_bus_doors(const render_data& data, bool snap);
    void render_flight_path(const glm::mat4& view, const glm::mat4& proj, const render_data& data, frame_profile* profile);
    void render_overlay(const glm::mat4& view_proj, const render_data& data, const text_assets& text, frame_profile* profile);
    void render_debug_overlay(const render_data& data, const text_assets& text, frame_profile* profile);
    void render_course_map(const render_data& data, const text_assets& text);
    // `picture` through the tape pass, leaving the tape target bound for the overlay.
    void render_tape(unsigned int picture, float seconds, float tracking);

    gpu_allocations allocations_;
    gl_storage storage_{&allocations_};
    SDL_Window* window_ = nullptr;
    overlay_pass overlay_pass_{storage_};
    backdrop_pass backdrop_pass_{storage_};
    texture grass_texture_{storage_};  // on the rough (terrain.frag)
    texture path_texture_{storage_};   // on the cart paths (terrain.frag)
    framebuffer scene_fbo_{storage_};
    camcorder_pass camcorder_pass_{storage_};
    eyepiece_pass eyepiece_pass_{storage_};
    bool eyepiece_masking_ = true;
    tape_pass tape_pass_{storage_};
    tv_pass tv_pass_{storage_};
    gl_timer_pool gpu_timers_;
    lighting_uniforms lighting_uniforms_{storage_};
    shader_program terrain_shader_{storage_};
    shader_program ball_shader_{storage_};
    tree_renderer tree_renderer_{storage_};
    hole_sign_renderer hole_sign_renderer_{storage_};
    fence_renderer fence_renderer_{storage_};
    floodlight_renderer floodlight_renderer_{storage_};
    water_renderer water_renderer_{storage_};
    reflection_pass reflection_pass_{storage_};
    weather_renderer weather_renderer_{storage_};
    world_marker_renderer world_marker_renderer_{storage_};
    world_marker_renderer tee_box_renderer_{storage_};
    world_marker_renderer cup_renderer_{storage_};
    prop_renderer prop_renderer_{storage_};
    blob_shadow_renderer blob_shadow_renderer_{storage_};
    shadow_map_pass shadow_map_pass_{storage_};
    lamp_shadow_pass lamp_shadow_pass_{storage_};
    sun_shaft_pass sun_shaft_pass_{storage_};
    // Rebuilt every frame; members so their vectors keep their capacity.
    world_marker_batch world_marker_batch_;
    world_marker_batch tee_box_batch_;
    world_marker_batch cup_batch_;
    prop_frame prop_frame_;
    std::uint64_t world_props_revision_ = 0;
    std::uint64_t static_casters_key_ = 0;
    std::uint64_t tee_boxes_key_ = 0;
    std::uint64_t cups_key_ = 0;
    std::uint64_t looks_revision_ = 0;
    // How far the bus's doors have swung: presentation only, from the state
    // render_data names.
    float bus_door_progress_ = 0.0f;
    bool bus_door_seeded_ = false;
    float bus_door_clock_ = 0.0f;
    prop_batch viewmodel_batch_;
    std::vector<render_terrain_vertex> flight_path_vertices_;
    course_map_renderer course_map_renderer_{storage_};
    dynamic_vertex_buffer flight_path_buffer_{storage_};
    unsigned int flight_path_vao_ = 0;
    renderer_cull_stats cull_stats_;

    terrain_renderer terrain_renderer_{storage_};
    grass_renderer grass_renderer_{storage_};
    primitive_buffers ground_;     // unit quad backdrop under the course
    primitive_buffers ball_;       // unit sphere
    unsigned int screen_vao_ = 0;  // full-screen quad for the screen passes
    unsigned int screen_vbo_ = 0;

    picture_style picture_style_;
    prop_catalog props_;
    water_style water_style_;
    grass_style grass_style_;
    terrain_lod_style terrain_lod_style_;
    scene_colors colors_;  // its glow floors this frame's (render_data::glow)
    picture_size picture_;  // the scene target's, for the window's current size
    int scene_preset_ = 0;  // the index into picture_style_.scene_lines
    overlay_grid ui_grid() const { return overlay_grid{picture_.ui_width, picture_.ui_height}; }
};
