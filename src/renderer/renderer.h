#pragma once

#include <SDL.h>

#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "renderer/framebuffer.h"
#include "renderer/shader.h"

struct render_data {
    glm::vec3 ball_position = glm::vec3(0.0f);
    glm::vec3 camera_position = glm::vec3(0.0f, 6.0f, -12.0f);
    glm::vec3 camera_target = glm::vec3(0.0f);
    glm::vec3 tee_position = glm::vec3(0.0f);
    glm::vec3 pin_position = glm::vec3(0.0f);
    std::vector<glm::vec3> aim_arc_points;
    float cup_radius = 0.75f;
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

    SDL_Window* window_ = nullptr;
    framebuffer scene_fbo_;
    shader_program terrain_shader_;
    shader_program ball_shader_;
    shader_program crt_shader_;

    unsigned int ground_vao_ = 0;
    unsigned int ground_vbo_ = 0;
    unsigned int ball_vao_ = 0;
    unsigned int ball_vbo_ = 0;
    unsigned int screen_vao_ = 0;
    unsigned int screen_vbo_ = 0;

    int target_width_ = 320;
    int target_height_ = 240;
};
