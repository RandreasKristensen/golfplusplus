#include "renderer/renderer.h"

#include <SDL.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

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

std::vector<float> make_disc_vertices(const int segments) {
    std::vector<float> vertices;
    vertices.reserve(static_cast<std::size_t>(segments) * 9);

    constexpr float radius = 0.5f;
    constexpr float pi = 3.14159265358979323846f;
    for (int i = 0; i < segments; ++i) {
        const float a0 = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = 2.0f * pi * static_cast<float>(i + 1) / static_cast<float>(segments);

        vertices.insert(vertices.end(), {
            0.0f, 0.0f, 0.0f,
            std::cos(a0) * radius, 0.0f, std::sin(a0) * radius,
            std::cos(a1) * radius, 0.0f, std::sin(a1) * radius
        });
    }

    return vertices;
}

void append_sphere_vertex(std::vector<float>& vertices, const glm::vec3 normal) {
    constexpr float radius = 1.0f;
    const glm::vec3 position = normal * radius;
    vertices.insert(vertices.end(), {
        position.x, position.y, position.z,
        normal.x, normal.y, normal.z
    });
}

std::vector<float> make_sphere_vertices(const int latitude_segments, const int longitude_segments) {
    std::vector<float> vertices;
    vertices.reserve(static_cast<std::size_t>(latitude_segments) *
                     static_cast<std::size_t>(longitude_segments) * 36);

    constexpr float pi = 3.14159265358979323846f;
    for (int lat = 0; lat < latitude_segments; ++lat) {
        const float theta0 = pi * static_cast<float>(lat) / static_cast<float>(latitude_segments);
        const float theta1 = pi * static_cast<float>(lat + 1) / static_cast<float>(latitude_segments);

        for (int lon = 0; lon < longitude_segments; ++lon) {
            const float phi0 = 2.0f * pi * static_cast<float>(lon) / static_cast<float>(longitude_segments);
            const float phi1 = 2.0f * pi * static_cast<float>(lon + 1) / static_cast<float>(longitude_segments);

            const glm::vec3 p00(std::sin(theta0) * std::cos(phi0), std::cos(theta0), std::sin(theta0) * std::sin(phi0));
            const glm::vec3 p01(std::sin(theta0) * std::cos(phi1), std::cos(theta0), std::sin(theta0) * std::sin(phi1));
            const glm::vec3 p10(std::sin(theta1) * std::cos(phi0), std::cos(theta1), std::sin(theta1) * std::sin(phi0));
            const glm::vec3 p11(std::sin(theta1) * std::cos(phi1), std::cos(theta1), std::sin(theta1) * std::sin(phi1));

            append_sphere_vertex(vertices, p00);
            append_sphere_vertex(vertices, p10);
            append_sphere_vertex(vertices, p11);

            append_sphere_vertex(vertices, p00);
            append_sphere_vertex(vertices, p11);
            append_sphere_vertex(vertices, p01);
        }
    }

    return vertices;
}

glm::vec3 color_for_material(const render_material_type type) {
    switch (type) {
    case render_material_type::green:
        return glm::vec3(0.20f, 0.68f, 0.28f);
    case render_material_type::bunker:
        return glm::vec3(0.66f, 0.55f, 0.22f);
    case render_material_type::water:
        return glm::vec3(0.10f, 0.22f, 0.60f);
    default:
        return glm::vec3(0.18f, 0.42f, 0.18f);
    }
}

void set_terrain_draw_state(shader_program& shader,
                            const glm::mat4& model,
                            const glm::mat4& view,
                            const glm::mat4& proj,
                            const glm::vec3& color,
                            const bool use_vertex_color) {
    shader.set_mat4("u_model", model);
    shader.set_mat4("u_mvp", proj * view * model);
    shader.set_vec3("u_color", color);
    shader.set_int("u_use_vertex_color", use_vertex_color ? 1 : 0);
}

glm::mat4 panel_model(const glm::vec3 center, const float yaw_degrees, const glm::vec3 scale) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
    model = glm::rotate(model, glm::radians(yaw_degrees), glm::vec3(0.0f, 1.0f, 0.0f));
    return glm::scale(model, scale);
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
    shader.set_mat4("u_model", glm::mat4(1.0f));
    shader.set_mat4("u_mvp", model);
    shader.set_vec3("u_color", color);
    shader.set_int("u_use_vertex_color", 0);
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

    if (terrain_mesh_ebo_ != 0) {
        glDeleteBuffers(1, &terrain_mesh_ebo_);
        terrain_mesh_ebo_ = 0;
    }

    if (terrain_mesh_vbo_ != 0) {
        glDeleteBuffers(1, &terrain_mesh_vbo_);
        terrain_mesh_vbo_ = 0;
    }

    if (terrain_mesh_vao_ != 0) {
        glDeleteVertexArrays(1, &terrain_mesh_vao_);
        terrain_mesh_vao_ = 0;
    }

    if (ball_vbo_ != 0) {
        glDeleteBuffers(1, &ball_vbo_);
        ball_vbo_ = 0;
    }

    if (ball_vao_ != 0) {
        glDeleteVertexArrays(1, &ball_vao_);
        ball_vao_ = 0;
    }

    if (marker_vbo_ != 0) {
        glDeleteBuffers(1, &marker_vbo_);
        marker_vbo_ = 0;
    }

    if (marker_vao_ != 0) {
        glDeleteVertexArrays(1, &marker_vao_);
        marker_vao_ = 0;
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

    glGenVertexArrays(1, &terrain_mesh_vao_);
    glGenBuffers(1, &terrain_mesh_vbo_);
    glGenBuffers(1, &terrain_mesh_ebo_);
    glBindVertexArray(terrain_mesh_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, terrain_mesh_vbo_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, terrain_mesh_ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(render_terrain_vertex),
                          reinterpret_cast<void*>(offsetof(render_terrain_vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(render_terrain_vertex),
                          reinterpret_cast<void*>(offsetof(render_terrain_vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(render_terrain_vertex),
                          reinterpret_cast<void*>(offsetof(render_terrain_vertex, color)));
    glBindVertexArray(0);

    const std::vector<float> ball_vertices = make_sphere_vertices(8, 12);
    ball_vertex_count_ = static_cast<int>(ball_vertices.size() / 6);

    glGenVertexArrays(1, &ball_vao_);
    glGenBuffers(1, &ball_vbo_);
    glBindVertexArray(ball_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, ball_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(ball_vertices.size() * sizeof(float)),
                 ball_vertices.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);

    const std::vector<float> marker_vertices = make_disc_vertices(18);
    marker_vertex_count_ = static_cast<int>(marker_vertices.size() / 3);

    glGenVertexArrays(1, &marker_vao_);
    glGenBuffers(1, &marker_vbo_);
    glBindVertexArray(marker_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, marker_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(marker_vertices.size() * sizeof(float)),
                 marker_vertices.data(),
                 GL_STATIC_DRAW);
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

void renderer::upload_terrain_mesh(const render_data& data) {
    if (terrain_mesh_vao_ == 0 || terrain_mesh_vbo_ == 0 || terrain_mesh_ebo_ == 0 ||
        data.terrain_vertices.empty() || data.terrain_indices.empty()) {
        terrain_mesh_index_count_ = 0;
        return;
    }

    terrain_mesh_index_count_ = static_cast<int>(data.terrain_indices.size());

    glBindVertexArray(terrain_mesh_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, terrain_mesh_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(data.terrain_vertices.size() * sizeof(render_terrain_vertex)),
                 data.terrain_vertices.data(),
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, terrain_mesh_ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(data.terrain_indices.size() * sizeof(std::uint32_t)),
                 data.terrain_indices.data(),
                 GL_DYNAMIC_DRAW);
    glBindVertexArray(0);
}

void renderer::render_scene(const glm::mat4& view, const glm::mat4& proj, const render_data& data) {
    upload_terrain_mesh(data);

    const float course_scale = std::max(1.0f, data.course_extent / 12.0f);
    const glm::mat4 ground_model = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.08f, 0.0f)),
                                              glm::vec3(course_scale, 1.0f, course_scale));

    terrain_shader_.use();
    terrain_shader_.set_vec3("u_light_dir", glm::normalize(glm::vec3(-0.35f, 0.80f, 0.42f)));
    set_terrain_draw_state(terrain_shader_, ground_model, view, proj, glm::vec3(0.10f, 0.26f, 0.13f), false);

    glBindVertexArray(ground_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    if (terrain_mesh_index_count_ > 0) {
        set_terrain_draw_state(terrain_shader_, glm::mat4(1.0f), view, proj, glm::vec3(0.18f, 0.42f, 0.18f), true);
        glBindVertexArray(terrain_mesh_vao_);
        glDrawElements(GL_TRIANGLES, terrain_mesh_index_count_, GL_UNSIGNED_INT, reinterpret_cast<void*>(0));
        glBindVertexArray(0);
    }

    for (const render_material_zone& zone : data.material_zones) {
        if (zone.has_radius) {
            const float zone_scale = std::max(1.0f, zone.radius * 2.0f);
            const glm::mat4 model = glm::scale(glm::translate(glm::mat4(1.0f),
                                                              zone.center + glm::vec3(0.0f, 0.025f, 0.0f)),
                                               glm::vec3(zone_scale, 1.0f, zone_scale));
            set_terrain_draw_state(terrain_shader_, model, view, proj, color_for_material(zone.type), false);
            glBindVertexArray(marker_vao_);
            glDrawArrays(GL_TRIANGLES, 0, marker_vertex_count_);
            glBindVertexArray(0);
        } else if (zone.has_bounds) {
            const glm::vec3 center = (zone.bounds_min + zone.bounds_max) * 0.5f + glm::vec3(0.0f, 0.025f, 0.0f);
            const glm::vec3 size = zone.bounds_max - zone.bounds_min;
            const glm::mat4 model = glm::scale(glm::translate(glm::mat4(1.0f), center),
                                               glm::vec3(std::max(0.05f, size.x / 24.0f), 1.0f, std::max(0.05f, size.z / 24.0f)));
            set_terrain_draw_state(terrain_shader_, model, view, proj, color_for_material(zone.type), false);
            glBindVertexArray(ground_vao_);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);
        }
    }

    const glm::mat4 tee_model = glm::scale(glm::translate(glm::mat4(1.0f),
                                                          data.tee_position + glm::vec3(0.0f, 0.01f, 0.0f)),
                                           glm::vec3(1.8f, 1.0f, 1.8f));
    set_terrain_draw_state(terrain_shader_, tee_model, view, proj, glm::vec3(0.45f, 0.30f, 0.16f), false);

    glBindVertexArray(marker_vao_);
    glDrawArrays(GL_TRIANGLES, 0, marker_vertex_count_);
    glBindVertexArray(0);

    const float cup_scale = std::max(data.cup_visual_radius_meters * 2.0f, data.cup_radius * 2.0f);
    const glm::mat4 cup_model = glm::scale(glm::translate(glm::mat4(1.0f),
                                                          data.pin_position + glm::vec3(0.0f, 0.02f, 0.0f)),
                                           glm::vec3(cup_scale, 1.0f, cup_scale));
    set_terrain_draw_state(terrain_shader_, cup_model, view, proj, glm::vec3(0.03f, 0.03f, 0.035f), false);

    glBindVertexArray(marker_vao_);
    glDrawArrays(GL_TRIANGLES, 0, marker_vertex_count_);
    glBindVertexArray(0);

    glBindVertexArray(screen_vao_);
    const float pin_height = std::max(0.1f, data.pin_visual_height_meters);
    const glm::vec3 pin_base = data.pin_position + glm::vec3(0.0f, pin_height * 0.5f, 0.0f);
    const glm::mat4 pin_pole = panel_model(pin_base, 0.0f, glm::vec3(0.045f, pin_height * 0.5f, 1.0f));
    const glm::mat4 pin_pole_cross = panel_model(pin_base, 90.0f, glm::vec3(0.045f, pin_height * 0.5f, 1.0f));
    set_terrain_draw_state(terrain_shader_, pin_pole, view, proj, glm::vec3(0.95f, 0.90f, 0.68f), false);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    set_terrain_draw_state(terrain_shader_, pin_pole_cross, view, proj, glm::vec3(0.95f, 0.90f, 0.68f), false);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    const glm::vec3 flag_center = data.pin_position + glm::vec3(0.34f, pin_height * 0.86f, 0.0f);
    const glm::mat4 flag_panel = panel_model(flag_center, 0.0f, glm::vec3(0.36f, 0.24f, 1.0f));
    const glm::mat4 flag_panel_cross = panel_model(flag_center, 90.0f, glm::vec3(0.36f, 0.24f, 1.0f));
    set_terrain_draw_state(terrain_shader_, flag_panel, view, proj, glm::vec3(0.96f, 0.78f, 0.20f), false);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    set_terrain_draw_state(terrain_shader_, flag_panel_cross, view, proj, glm::vec3(0.96f, 0.78f, 0.20f), false);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    if (data.show_aim_indicator && !data.aim_arc_points.empty()) {
        glBindVertexArray(marker_vao_);
        for (std::size_t i = 0; i < data.aim_arc_points.size(); ++i) {
            const float scale = 0.35f + static_cast<float>(i % 3) * 0.04f;
            const glm::mat4 arc_model = glm::scale(glm::translate(glm::mat4(1.0f), data.aim_arc_points[i] + glm::vec3(0.0f, 0.05f, 0.0f)),
                                                   glm::vec3(scale, 1.0f, scale));
            set_terrain_draw_state(terrain_shader_, arc_model, view, proj, glm::vec3(0.95f, 0.78f, 0.22f), false);
            glDrawArrays(GL_TRIANGLES, 0, marker_vertex_count_);
        }
        glBindVertexArray(0);
    }

    const float ball_radius = std::max(0.02f, data.ball_visual_radius_meters);
    const glm::mat4 ball_model = glm::scale(glm::translate(glm::mat4(1.0f),
                                                           data.ball_position + glm::vec3(0.0f, ball_radius, 0.0f)),
                                            glm::vec3(ball_radius));
    const glm::mat4 ball_mvp = proj * view * ball_model;

    ball_shader_.use();
    ball_shader_.set_mat4("u_mvp", ball_mvp);
    ball_shader_.set_mat4("u_model", ball_model);
    ball_shader_.set_vec3("u_color", glm::vec3(0.9f, 0.9f, 0.9f));
    ball_shader_.set_vec3("u_light_dir", glm::normalize(glm::vec3(-0.35f, 0.75f, 0.45f)));

    glBindVertexArray(ball_vao_);
    glDrawArrays(GL_TRIANGLES, 0, ball_vertex_count_);
    glBindVertexArray(0);
}

void renderer::render_overlay(const render_data& data) {
    glDisable(GL_DEPTH_TEST);

    terrain_shader_.use();
    terrain_shader_.set_mat4("u_model", glm::mat4(1.0f));
    terrain_shader_.set_int("u_use_vertex_color", 0);
    terrain_shader_.set_vec3("u_light_dir", glm::vec3(0.0f, 1.0f, 0.0f));
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
