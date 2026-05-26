#pragma once

#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "game/scorecard.h"
#include "physics/material_zone.h"
#include "renderer/framebuffer.h"
#include "renderer/shader.h"

struct render_terrain_vertex {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 color = glm::vec3(0.18f, 0.42f, 0.18f);
};

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

struct render_tree {
    glm::vec3 base = glm::vec3(0.0f);
    float trunk_radius = 0.35f;
    float trunk_height = 2.4f;
    float leaf_radius = 1.6f;
    float leaf_height = 3.2f;
};

enum class startup_menu_screen {
    none,
    main,
    help,
    hole_picker,
    course_picker
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

struct render_data {
    glm::vec3 ball_position = glm::vec3(0.0f);
    glm::vec3 player_position = glm::vec3(0.0f);
    float player_yaw = 0.0f;
    glm::vec3 camera_position = glm::vec3(0.0f, 6.0f, -12.0f);
    glm::vec3 camera_target = glm::vec3(0.0f);
    glm::vec3 tee_position = glm::vec3(0.0f);
    glm::vec3 pin_position = glm::vec3(0.0f);
    std::vector<glm::vec3> aim_arc_points;
    std::vector<glm::vec3> flight_path_points;
    std::vector<render_tree> trees;
    std::vector<render_terrain_vertex> terrain_vertices;
    std::vector<std::uint32_t> terrain_indices;
    std::vector<render_terrain_vertex> material_overlay_vertices;
    std::vector<std::uint32_t> material_overlay_indices;
    float cup_radius = 0.65f;
    float ball_visual_radius_meters = 0.10f;
    float cup_visual_radius_meters = 0.75f;
    float pin_visual_height_meters = 2.10f;
    float course_extent = 100.0f;
    float aim_angle = 0.0f;
    float camera_fov_degrees = 60.0f;
    bool ball_moving = false;
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
    bool show_course_results = false;
    scorecard_data scorecard;
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
    controls_overlay_state controls;
    render_startup_menu startup_menu;
};

struct renderer {
    bool init(SDL_Window* window);
    void shutdown();
    void render(const render_data& data);

private:
    bool init_shaders();
    bool init_geometry();
    bool init_framebuffer();
    void render_scene(const glm::mat4& view, const glm::mat4& proj, const render_data& data);
    void render_overlay(const glm::mat4& view, const glm::mat4& proj, const render_data& data);
    void render_crt(int screen_width, int screen_height);
    void upload_terrain_mesh(const render_data& data);
    void upload_material_overlay_mesh(const render_data& data);

    SDL_Window* window_ = nullptr;
    framebuffer scene_fbo_;
    shader_program terrain_shader_;
    shader_program ball_shader_;
    shader_program crt_shader_;

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
    unsigned int flight_path_vbo_ = 0;
    unsigned int marker_vao_ = 0;
    unsigned int marker_vbo_ = 0;
    unsigned int cylinder_vao_ = 0;
    unsigned int cylinder_vbo_ = 0;
    unsigned int cone_vao_ = 0;
    unsigned int cone_vbo_ = 0;
    unsigned int screen_vao_ = 0;
    unsigned int screen_vbo_ = 0;
    int ball_vertex_count_ = 0;
    int marker_vertex_count_ = 0;
    int cylinder_vertex_count_ = 0;
    int cone_vertex_count_ = 0;
    int terrain_mesh_index_count_ = 0;
    int material_overlay_index_count_ = 0;

    int target_width_ = 640;
    int target_height_ = 360;
};
