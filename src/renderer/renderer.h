#pragma once

#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "renderer/framebuffer.h"
#include "renderer/shader.h"

enum class render_material_type {
    green,
    bunker,
    water,
    unknown
};

struct render_material_zone {
    render_material_type type = render_material_type::unknown;
    glm::vec3 center = glm::vec3(0.0f);
    float radius = 0.0f;
    glm::vec3 bounds_min = glm::vec3(0.0f);
    glm::vec3 bounds_max = glm::vec3(0.0f);
    bool has_radius = false;
    bool has_bounds = false;
};

struct render_terrain_vertex {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 color = glm::vec3(0.18f, 0.42f, 0.18f);
};

struct render_data {
    glm::vec3 ball_position = glm::vec3(0.0f);
    glm::vec3 camera_position = glm::vec3(0.0f, 6.0f, -12.0f);
    glm::vec3 camera_target = glm::vec3(0.0f);
    glm::vec3 tee_position = glm::vec3(0.0f);
    glm::vec3 pin_position = glm::vec3(0.0f);
    std::vector<glm::vec3> aim_arc_points;
    std::vector<render_terrain_vertex> terrain_vertices;
    std::vector<std::uint32_t> terrain_indices;
    std::vector<render_material_zone> material_zones;
    float cup_radius = 0.053975f;
    float ball_visual_radius_meters = 0.10f;
    float cup_visual_radius_meters = 0.10f;
    float pin_visual_height_meters = 2.10f;
    float course_extent = 100.0f;
    float aim_angle = 0.0f;
    bool ball_moving = false;
    bool show_interact_prompt = false;
    bool show_aim_indicator = false;
    bool swing_timing = false;
    float swing_power = 0.0f;
    int stroke_count = 0;
    std::string selected_club_label;
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
    void render_overlay(const render_data& data);
    void render_crt(int screen_width, int screen_height);
    void upload_terrain_mesh(const render_data& data);

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
    unsigned int ball_vao_ = 0;
    unsigned int ball_vbo_ = 0;
    unsigned int marker_vao_ = 0;
    unsigned int marker_vbo_ = 0;
    unsigned int screen_vao_ = 0;
    unsigned int screen_vbo_ = 0;
    int ball_vertex_count_ = 0;
    int marker_vertex_count_ = 0;
    int terrain_mesh_index_count_ = 0;

    int target_width_ = 640;
    int target_height_ = 360;
};
