#pragma once

#include <SDL.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "renderer/framebuffer.h"
#include "renderer/shader.h"

struct renderer {
    bool init(SDL_Window* window);
    void shutdown();
    void render(const glm::vec3& ball_position);

private:
    bool init_shaders();
    bool init_geometry();
    bool init_framebuffer();
    void render_scene(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& ball_position);
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
