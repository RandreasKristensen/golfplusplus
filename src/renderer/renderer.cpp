#include "renderer/renderer.h"

#include <SDL.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include "core/gl_loader.h"

namespace {
std::string asset_path(const char* relative) {
#ifdef VCR_GOLF_ASSETS_DIR
    std::string base = VCR_GOLF_ASSETS_DIR;
#else
    std::string base = "assets";
#endif
    if (!base.empty() && base.back() != '/' && base.back() != '\\') {
        base.push_back('/');
    }
    return base + relative;
}

glm::vec3 aim_direction(const float aim_angle) {
    return glm::normalize(glm::vec3(std::sin(aim_angle), 0.0f, std::cos(aim_angle)));
}

const std::array<const char*, 7>& glyph_rows(const char value) {
    static const std::array<const char*, 7> glyph_p = {
        "1110",
        "1001",
        "1001",
        "1110",
        "1000",
        "1000",
        "1000"
    };
    static const std::array<const char*, 7> glyph_w = {
        "10001",
        "10001",
        "10001",
        "10101",
        "10101",
        "10101",
        "01010"
    };
    static const std::array<const char*, 7> glyph_7 = {
        "1111",
        "0001",
        "0010",
        "0010",
        "0100",
        "0100",
        "0100"
    };
    static const std::array<const char*, 7> glyph_i = {
        "111",
        "010",
        "010",
        "010",
        "010",
        "010",
        "111"
    };
    static const std::array<const char*, 7> glyph_s = {
        "1111",
        "1000",
        "1000",
        "1110",
        "0001",
        "0001",
        "1110"
    };
    static const std::array<const char*, 7> glyph_c = {
        "0111",
        "1000",
        "1000",
        "1000",
        "1000",
        "1000",
        "0111"
    };

    switch (value) {
    case 'P':
        return glyph_p;
    case 'W':
        return glyph_w;
    case '7':
        return glyph_7;
    case 'S':
        return glyph_s;
    case 'C':
        return glyph_c;
    default:
        return glyph_i;
    }
}

int glyph_width(const char value) {
    return static_cast<int>(std::string(glyph_rows(value)[0]).size());
}

void draw_overlay_quad(shader_program& shader, const glm::vec2 center, const glm::vec2 half_size, const glm::vec3 color) {
    const glm::mat4 model = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f)),
                                       glm::vec3(half_size, 1.0f));
    shader.set_mat4("u_mvp", model);
    shader.set_vec3("u_color", color);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void draw_pixel_glyph(shader_program& shader,
                      const char value,
                      const glm::vec2 top_left,
                      const float pixel_size,
                      const glm::vec3 color) {
    const std::array<const char*, 7>& rows = glyph_rows(value);
    for (int y = 0; y < static_cast<int>(rows.size()); ++y) {
        const char* row = rows[y];
        for (int x = 0; row[x] != '\0'; ++x) {
            if (row[x] != '1') {
                continue;
            }

            draw_overlay_quad(shader,
                              top_left + glm::vec2((static_cast<float>(x) + 0.5f) * pixel_size,
                                                   -(static_cast<float>(y) + 0.5f) * pixel_size),
                              glm::vec2(pixel_size * 0.42f),
                              color);
        }
    }
}

void draw_club_label(shader_program& shader, const std::string& label) {
    const glm::vec3 label_color(0.90f, 0.88f, 0.76f);
    const glm::vec3 panel_color(0.055f, 0.06f, 0.07f);
    draw_overlay_quad(shader, glm::vec2(0.78f, 0.78f), glm::vec2(0.17f, 0.12f), panel_color);

    const float pixel_size = 0.025f;
    float width = 0.0f;
    for (const char c : label) {
        width += static_cast<float>(glyph_width(c) + 1) * pixel_size;
    }
    width = std::max(0.0f, width - pixel_size);

    glm::vec2 cursor(0.78f - width * 0.5f, 0.86f);
    for (const char c : label) {
        draw_pixel_glyph(shader, c, cursor, pixel_size, label_color);
        cursor.x += static_cast<float>(glyph_width(c) + 1) * pixel_size;
    }
}

void draw_interact_prompt(shader_program& shader) {
    const glm::vec3 prompt_color(0.95f, 0.82f, 0.28f);
    const glm::vec3 panel_color(0.05f, 0.055f, 0.06f);
    draw_overlay_quad(shader, glm::vec2(0.0f, -0.56f), glm::vec2(0.19f, 0.075f), panel_color);

    const std::string label = "SPC";
    const float pixel_size = 0.021f;
    float width = 0.0f;
    for (const char c : label) {
        width += static_cast<float>(glyph_width(c) + 1) * pixel_size;
    }
    width = std::max(0.0f, width - pixel_size);

    glm::vec2 cursor(-width * 0.5f, -0.51f);
    for (const char c : label) {
        draw_pixel_glyph(shader, c, cursor, pixel_size, prompt_color);
        cursor.x += static_cast<float>(glyph_width(c) + 1) * pixel_size;
    }
}
}

bool renderer::init(SDL_Window* window) {
    window_ = window;
    if (!window_) {
        return false;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    if (!init_framebuffer()) {
        return false;
    }

    if (!init_shaders()) {
        return false;
    }

    if (!init_geometry()) {
        return false;
    }

    return true;
}

void renderer::shutdown() {
    terrain_shader_.shutdown();
    ball_shader_.shutdown();
    crt_shader_.shutdown();

    if (ground_vbo_ != 0) {
        glDeleteBuffers(1, &ground_vbo_);
        ground_vbo_ = 0;
    }

    if (ground_vao_ != 0) {
        glDeleteVertexArrays(1, &ground_vao_);
        ground_vao_ = 0;
    }

    if (ball_vbo_ != 0) {
        glDeleteBuffers(1, &ball_vbo_);
        ball_vbo_ = 0;
    }

    if (ball_vao_ != 0) {
        glDeleteVertexArrays(1, &ball_vao_);
        ball_vao_ = 0;
    }

    if (screen_vbo_ != 0) {
        glDeleteBuffers(1, &screen_vbo_);
        screen_vbo_ = 0;
    }

    if (screen_vao_ != 0) {
        glDeleteVertexArrays(1, &screen_vao_);
        screen_vao_ = 0;
    }

    scene_fbo_.shutdown();
    window_ = nullptr;
}

void renderer::render(const render_data& data) {
    if (!window_) {
        return;
    }

    int screen_width = 0;
    int screen_height = 0;
    SDL_GL_GetDrawableSize(window_, &screen_width, &screen_height);

    glEnable(GL_DEPTH_TEST);

    scene_fbo_.bind();
    glViewport(0, 0, target_width_, target_height_);
    glClearColor(0.36f, 0.56f, 0.82f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                            static_cast<float>(target_width_) / static_cast<float>(target_height_),
                                            0.1f,
                                            std::max(160.0f, data.course_extent * 2.5f));
    const glm::mat4 view = glm::lookAt(data.camera_position,
                                       data.camera_target,
                                       glm::vec3(0.0f, 1.0f, 0.0f));

    render_scene(view, proj, data);
    render_overlay(data);

    framebuffer::bind_default();
    render_crt(screen_width, screen_height);
}

bool renderer::init_shaders() {
    const std::string terrain_vert = asset_path("shaders/terrain.vert");
    const std::string terrain_frag = asset_path("shaders/terrain.frag");
    const std::string ball_vert = asset_path("shaders/ball.vert");
    const std::string ball_frag = asset_path("shaders/ball.frag");
    const std::string crt_vert = asset_path("shaders/crt.vert");
    const std::string crt_frag = asset_path("shaders/crt.frag");

    if (!terrain_shader_.load_from_files(terrain_vert.c_str(), terrain_frag.c_str())) {
        return false;
    }

    if (!ball_shader_.load_from_files(ball_vert.c_str(), ball_frag.c_str())) {
        return false;
    }

    if (!crt_shader_.load_from_files(crt_vert.c_str(), crt_frag.c_str())) {
        return false;
    }

    return true;
}

bool renderer::init_geometry() {
    const float ground_vertices[] = {
        -12.0f, 0.0f, -12.0f,
         12.0f, 0.0f, -12.0f,
         12.0f, 0.0f,  12.0f,
        -12.0f, 0.0f, -12.0f,
         12.0f, 0.0f,  12.0f,
        -12.0f, 0.0f,  12.0f
    };

    glGenVertexArrays(1, &ground_vao_);
    glGenBuffers(1, &ground_vbo_);
    glBindVertexArray(ground_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, ground_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(ground_vertices), ground_vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));
    glBindVertexArray(0);

    const float ball_vertices[] = {
        -0.25f, 0.0f, -0.25f,
         0.25f, 0.0f, -0.25f,
         0.25f, 0.0f,  0.25f,
        -0.25f, 0.0f, -0.25f,
         0.25f, 0.0f,  0.25f,
        -0.25f, 0.0f,  0.25f
    };

    glGenVertexArrays(1, &ball_vao_);
    glGenBuffers(1, &ball_vbo_);
    glBindVertexArray(ball_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, ball_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(ball_vertices), ball_vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));
    glBindVertexArray(0);

    const float screen_vertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 0.0f, 1.0f
    };

    glGenVertexArrays(1, &screen_vao_);
    glGenBuffers(1, &screen_vbo_);
    glBindVertexArray(screen_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, screen_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(screen_vertices), screen_vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);

    return true;
}

bool renderer::init_framebuffer() {
    return scene_fbo_.init(target_width_, target_height_);
}

void renderer::render_scene(const glm::mat4& view, const glm::mat4& proj, const render_data& data) {
    const float course_scale = std::max(1.0f, data.course_extent / 12.0f);
    const glm::mat4 ground_model = glm::scale(glm::mat4(1.0f), glm::vec3(course_scale, 1.0f, course_scale));
    const glm::mat4 ground_mvp = proj * view * ground_model;

    terrain_shader_.use();
    terrain_shader_.set_mat4("u_mvp", ground_mvp);
    terrain_shader_.set_vec3("u_color", glm::vec3(0.15f, 0.55f, 0.25f));

    glBindVertexArray(ground_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    const glm::mat4 tee_model = glm::scale(glm::translate(glm::mat4(1.0f),
                                                          data.tee_position + glm::vec3(0.0f, 0.01f, 0.0f)),
                                           glm::vec3(1.8f, 1.0f, 1.8f));
    terrain_shader_.set_mat4("u_mvp", proj * view * tee_model);
    terrain_shader_.set_vec3("u_color", glm::vec3(0.45f, 0.30f, 0.16f));

    glBindVertexArray(ball_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    const float cup_scale = std::max(0.2f, data.cup_radius * 2.0f);
    const glm::mat4 cup_model = glm::scale(glm::translate(glm::mat4(1.0f),
                                                          data.pin_position + glm::vec3(0.0f, 0.02f, 0.0f)),
                                           glm::vec3(cup_scale, 1.0f, cup_scale));
    terrain_shader_.set_mat4("u_mvp", proj * view * cup_model);
    terrain_shader_.set_vec3("u_color", glm::vec3(0.03f, 0.03f, 0.035f));

    glBindVertexArray(ball_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    const glm::mat4 pin_model = glm::scale(glm::translate(glm::mat4(1.0f),
                                                          data.pin_position + glm::vec3(0.0f, 2.2f, 0.0f)),
                                           glm::vec3(0.12f, 2.2f, 1.0f));
    terrain_shader_.set_mat4("u_mvp", proj * view * pin_model);
    terrain_shader_.set_vec3("u_color", glm::vec3(0.95f, 0.85f, 0.2f));

    glBindVertexArray(screen_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    if (data.show_aim_indicator && !data.aim_arc_points.empty()) {
        terrain_shader_.set_vec3("u_color", glm::vec3(0.95f, 0.78f, 0.22f));
        glBindVertexArray(ball_vao_);
        for (std::size_t i = 0; i < data.aim_arc_points.size(); ++i) {
            const float scale = 0.35f + static_cast<float>(i % 3) * 0.04f;
            const glm::mat4 arc_model = glm::scale(glm::translate(glm::mat4(1.0f), data.aim_arc_points[i] + glm::vec3(0.0f, 0.05f, 0.0f)),
                                                   glm::vec3(scale, 1.0f, scale));
            terrain_shader_.set_mat4("u_mvp", proj * view * arc_model);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        glBindVertexArray(0);
    }

    const glm::mat4 ball_model = glm::translate(glm::mat4(1.0f), data.ball_position + glm::vec3(0.0f, 0.25f, 0.0f));
    const glm::mat4 ball_mvp = proj * view * ball_model;

    ball_shader_.use();
    ball_shader_.set_mat4("u_mvp", ball_mvp);
    ball_shader_.set_vec3("u_color", glm::vec3(0.9f, 0.9f, 0.9f));

    glBindVertexArray(ball_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void renderer::render_overlay(const render_data& data) {
    glDisable(GL_DEPTH_TEST);

    terrain_shader_.use();
    glBindVertexArray(screen_vao_);

    draw_club_label(terrain_shader_, data.selected_club_label);

    if (data.show_interact_prompt) {
        draw_interact_prompt(terrain_shader_);
    }

    if (data.swing_timing) {
        const float power = std::max(0.0f, std::min(data.swing_power, 1.0f));
        const glm::mat4 back_model = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-0.56f, -0.82f, 0.0f)),
                                                glm::vec3(0.62f, 0.045f, 1.0f));
        terrain_shader_.set_mat4("u_mvp", back_model);
        terrain_shader_.set_vec3("u_color", glm::vec3(0.08f, 0.08f, 0.10f));
        glDrawArrays(GL_TRIANGLES, 0, 6);

        const glm::mat4 fill_model = glm::scale(glm::translate(glm::mat4(1.0f),
                                                               glm::vec3(-1.18f + power * 0.62f, -0.82f, 0.0f)),
                                                glm::vec3(0.62f * power, 0.032f, 1.0f));
        terrain_shader_.set_mat4("u_mvp", fill_model);
        terrain_shader_.set_vec3("u_color", glm::vec3(0.92f, 0.70f, 0.18f));
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    const int strokes = std::max(data.stroke_count, 0);
    const int ten_marks = std::min(strokes / 10, 8);
    for (int i = 0; i < ten_marks; ++i) {
        const float x = -0.92f + static_cast<float>(i) * 0.07f;
        const glm::mat4 tick_model = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.86f, 0.0f)),
                                                glm::vec3(0.026f, 0.08f, 1.0f));
        terrain_shader_.set_mat4("u_mvp", tick_model);
        terrain_shader_.set_vec3("u_color", glm::vec3(0.92f, 0.70f, 0.18f));
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    const int one_marks = strokes % 10;
    for (int i = 0; i < one_marks; ++i) {
        const float x = -0.92f + static_cast<float>(i) * 0.055f;
        const glm::mat4 tick_model = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.76f, 0.0f)),
                                                glm::vec3(0.015f, 0.06f, 1.0f));
        terrain_shader_.set_mat4("u_mvp", tick_model);
        terrain_shader_.set_vec3("u_color", glm::vec3(0.88f, 0.88f, 0.78f));
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void renderer::render_crt(int screen_width, int screen_height) {
    glViewport(0, 0, screen_width, screen_height);
    glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glDisable(GL_DEPTH_TEST);

    crt_shader_.use();
    crt_shader_.set_int("u_scene", 0);
    crt_shader_.set_vec2("u_resolution", glm::vec2(static_cast<float>(screen_width),
                                                    static_cast<float>(screen_height)));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_fbo_.color_texture());

    glBindVertexArray(screen_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}
