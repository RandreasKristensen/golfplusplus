#pragma once

#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "game/scorecard.h"
#include "physics/material_zone.h"
#include "profiling/profiling.h"
#include "renderer/course_map_fill.h"
#include "renderer/dynamic_buffer.h"
#include "renderer/framebuffer.h"
#include "renderer/gl_timer.h"
#include "renderer/overlay_pass.h"
#include "renderer/render_mesh.h"
#include "renderer/render_tree.h"
#include "renderer/shader.h"
#include "renderer/tree_renderer.h"
#include "renderer/world_marker_batch.h"
#include "renderer/world_marker_renderer.h"

struct controls_overlay_state {
    bool visible = true;
    bool key_1_down = false;
    bool key_2_down = false;
    bool left_down = false;
    bool right_down = false;
    bool up_down = false;
    bool down_down = false;
    bool space_down = false;
    bool shift_down = false;
    bool enter_down = false;
    bool backspace_down = false;
    bool retee_down = false;
};

enum class startup_menu_screen {
    none,
    main,
    help,
    hole_picker,
    course_picker,
    shop_picker,
    shop_inventory
};

struct render_hole_preview {
    glm::vec3 tee_position = glm::vec3(0.0f);
    glm::vec3 pin_position = glm::vec3(0.0f);
    std::vector<glm::vec3> control_points;
    float fairway_width = 0.0f;
    std::vector<material_zone> material_zones;
};

struct render_startup_tile {
    std::string title;
    std::string subtitle;
    bool selected = false;
    bool has_preview = false;
    render_hole_preview preview;
};

struct render_startup_menu {
    startup_menu_screen screen = startup_menu_screen::none;
    std::string title;
    std::string subtitle;
    std::string footer;
    std::vector<render_startup_tile> tiles;
};

struct render_skill_progress {
    std::string label;
    int level = 1;
    int xp = 0;
    int xp_to_next = 0;
};

enum class skill_icon_id {
    golf_swing,
    smoking,
    fitness,
    generic
};

struct render_xp_drop {
    skill_icon_id icon = skill_icon_id::generic;
    int xp = 0;
    float age = 0.0f;
    float lifetime = 2.4f;
};

struct render_data {
    glm::vec3 ball_position = glm::vec3(0.0f);
    glm::vec3 player_position = glm::vec3(0.0f);
    float player_yaw = 0.0f;
    glm::vec3 camera_position = glm::vec3(0.0f, 6.0f, -12.0f);
    glm::vec3 camera_target = glm::vec3(0.0f);
    glm::vec3 tee_position = glm::vec3(0.0f);
    glm::vec3 pin_position = glm::vec3(0.0f);
    // Borrowed, never owned: these point at caches owned by the game/app that
    // outlive the render call, so building a frame copies no vectors. Null
    // means "none this frame".
    const std::vector<glm::vec3>* tee_markers = nullptr;
    const std::vector<glm::vec3>* pin_markers = nullptr;
    const std::vector<glm::vec3>* start_markers = nullptr;
    const std::vector<glm::vec3>* flight_path_points = nullptr;
    const std::vector<render_tree>* trees = nullptr;
    // Built per frame while aiming, so it stays a value.
    std::vector<glm::vec3> aim_arc_points;
    // Instance data for `trees` is re-uploaded only when this (or the tree count) changes.
    std::uint64_t trees_revision = 0;
    const render_static_mesh* terrain_mesh = nullptr;
    const render_static_mesh* material_overlay_mesh = nullptr;
    float cup_radius = 0.65f;
    float ball_visual_radius_meters = 0.10f;
    float cup_visual_radius_meters = 0.75f;
    float pin_visual_height_meters = 2.10f;
    float course_extent = 100.0f;
    float aim_angle = 0.0f;
    float camera_fov_degrees = 60.0f;
    bool ball_moving = false;
    bool show_primary_hole_markers = true;
    bool show_flight_path = false;
    glm::vec3 flight_path_color = glm::vec3(0.92f, 0.18f, 0.16f);
    float flight_path_alpha = 0.45f;
    float flight_path_width = 1.0f;
    bool show_interact_prompt = false;
    bool show_aim_indicator = false;
    bool shot_addressing = false;
    bool swing_timing = false;
    bool show_power_meter = false;
    float swing_power = 0.0f;
    int stroke_count = 0;
    std::string selected_club_label;
    bool show_rangefinder = false;
    float rangefinder_distance_meters = 0.0f;
    std::string rangefinder_distance_label;
    bool show_course_map = false;
    bool show_scorecard = false;
    bool show_skills_panel = false;
    bool show_course_results = false;
    scorecard_data scorecard;
    std::vector<render_skill_progress> skills;
    std::vector<render_xp_drop> xp_drops;
    bool cart_active = false;
    bool cart_drifting = false;
    float cart_yaw = 0.0f;
    float cart_speed = 0.0f;
    bool smoke_emote_active = false;
    bool beer_emote_active = false;
    float smoke_emote_elapsed = 0.0f;
    float beer_emote_elapsed = 0.0f;
    bool show_fps = false;
    std::string fps_label;
    // Averaged profiling summary rendered by the debug overlay. Only drawn when
    // show_fps is set, so the normal UI is untouched when FPS display is off.
    frame_profile profile_summary;
    controls_overlay_state controls;
    render_startup_menu startup_menu;
};

// Borrowed-list accessors: an empty list stands in for a null pointer.
inline const std::vector<render_tree>& render_trees(const render_data& data) {
    static const std::vector<render_tree> none;
    return data.trees != nullptr ? *data.trees : none;
}

inline const std::vector<glm::vec3>& flight_path_points_or_empty(const render_data& data) {
    static const std::vector<glm::vec3> none;
    return data.flight_path_points != nullptr ? *data.flight_path_points : none;
}

struct renderer {
    bool init(SDL_Window* window);
    void shutdown();
    // `profile` is optional: null disables all profiling work.
    void render(const render_data& data, frame_profile* profile = nullptr);

private:
    bool init_shaders();
    bool init_geometry();
    bool init_framebuffer();
    bool ensure_framebuffer_size(int screen_width, int screen_height);
    void render_scene(const glm::mat4& view, const glm::mat4& proj, const render_data& data, frame_profile* profile);
    void render_overlay(const glm::mat4& view, const glm::mat4& proj, const render_data& data, frame_profile* profile);
    void render_crt(int screen_width, int screen_height, frame_profile* profile);
    void upload_terrain_mesh(const render_data& data, frame_profile* profile);
    void upload_material_overlay_mesh(const render_data& data, frame_profile* profile);

    SDL_Window* window_ = nullptr;
    // Batched 2D overlay (HUD, menus, pixel text): one dynamic VBO, one draw.
    overlay_pass overlay_pass_;
    framebuffer scene_fbo_;
    gl_timer_pool gpu_timers_;
    shader_program terrain_shader_;
    shader_program ball_shader_;
    shader_program crt_shader_;
    tree_renderer tree_renderer_;
    world_marker_renderer world_marker_renderer_;
    // Rebuilt every frame; kept as a member so its vectors keep their capacity.
    world_marker_batch world_marker_batch_;
    // Course map terrain fill: rebuilt only when the map layout or the terrain
    // mesh changes, and uploaded only on rebuild.
    course_map_fill_cache course_map_fill_cache_;
    // Reused CPU staging for the flight path line strip.
    std::vector<render_terrain_vertex> flight_path_vertices_;
    dynamic_vertex_buffer flight_path_buffer_;

    unsigned int ground_vao_ = 0;
    unsigned int ground_vbo_ = 0;
    unsigned int terrain_mesh_vao_ = 0;
    unsigned int terrain_mesh_vbo_ = 0;
    unsigned int terrain_mesh_ebo_ = 0;
    unsigned int material_overlay_vao_ = 0;
    unsigned int material_overlay_vbo_ = 0;
    unsigned int material_overlay_ebo_ = 0;
    unsigned int ball_vao_ = 0;
    unsigned int ball_vbo_ = 0;
    unsigned int flight_path_vao_ = 0;
    unsigned int cylinder_vao_ = 0;
    unsigned int cylinder_vbo_ = 0;
    // Only ever drawn instanced through tree_renderer, which owns its VAO.
    unsigned int cone_vbo_ = 0;
    unsigned int screen_vao_ = 0;
    unsigned int screen_vbo_ = 0;
    int ball_vertex_count_ = 0;
    int cylinder_vertex_count_ = 0;
    int cone_vertex_count_ = 0;
    int terrain_mesh_index_count_ = 0;
    int material_overlay_index_count_ = 0;
    std::uint64_t uploaded_terrain_revision_ = 0;
    std::uint64_t uploaded_material_overlay_revision_ = 0;
    bool terrain_mesh_uploaded_ = false;
    bool material_overlay_mesh_uploaded_ = false;

    int target_width_ = 640;
    int target_height_ = 360;
};
