#include "renderer/renderer.h"

#include <SDL.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cctype>
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

void append_mesh_vertex(std::vector<float>& vertices, const glm::vec3 position, const glm::vec3 normal) {
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

std::vector<float> make_cylinder_vertices(const int segments) {
    std::vector<float> vertices;
    vertices.reserve(static_cast<std::size_t>(segments) * 72);

    constexpr float pi = 3.14159265358979323846f;
    for (int i = 0; i < segments; ++i) {
        const float a0 = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = 2.0f * pi * static_cast<float>(i + 1) / static_cast<float>(segments);
        const glm::vec3 n0(std::cos(a0), 0.0f, std::sin(a0));
        const glm::vec3 n1(std::cos(a1), 0.0f, std::sin(a1));
        const glm::vec3 p00(n0.x, 0.0f, n0.z);
        const glm::vec3 p01(n1.x, 0.0f, n1.z);
        const glm::vec3 p10(n0.x, 1.0f, n0.z);
        const glm::vec3 p11(n1.x, 1.0f, n1.z);

        append_mesh_vertex(vertices, p00, n0);
        append_mesh_vertex(vertices, p01, n1);
        append_mesh_vertex(vertices, p11, n1);
        append_mesh_vertex(vertices, p00, n0);
        append_mesh_vertex(vertices, p11, n1);
        append_mesh_vertex(vertices, p10, n0);

        append_mesh_vertex(vertices, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
        append_mesh_vertex(vertices, p01, glm::vec3(0.0f, -1.0f, 0.0f));
        append_mesh_vertex(vertices, p00, glm::vec3(0.0f, -1.0f, 0.0f));

        append_mesh_vertex(vertices, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        append_mesh_vertex(vertices, p10, glm::vec3(0.0f, 1.0f, 0.0f));
        append_mesh_vertex(vertices, p11, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    return vertices;
}

std::vector<float> make_cone_vertices(const int segments) {
    std::vector<float> vertices;
    vertices.reserve(static_cast<std::size_t>(segments) * 54);

    constexpr float pi = 3.14159265358979323846f;
    const glm::vec3 tip(0.0f, 1.0f, 0.0f);
    for (int i = 0; i < segments; ++i) {
        const float a0 = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = 2.0f * pi * static_cast<float>(i + 1) / static_cast<float>(segments);
        const glm::vec3 p0(std::cos(a0), 0.0f, std::sin(a0));
        const glm::vec3 p1(std::cos(a1), 0.0f, std::sin(a1));
        const glm::vec3 face_normal = glm::normalize(glm::cross(p1 - p0, tip - p0));

        append_mesh_vertex(vertices, p0, face_normal);
        append_mesh_vertex(vertices, p1, face_normal);
        append_mesh_vertex(vertices, tip, face_normal);

        append_mesh_vertex(vertices, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
        append_mesh_vertex(vertices, p0, glm::vec3(0.0f, -1.0f, 0.0f));
        append_mesh_vertex(vertices, p1, glm::vec3(0.0f, -1.0f, 0.0f));
    }

    return vertices;
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
    shader.set_float("u_alpha", 1.0f);
    shader.set_int("u_use_vertex_color", use_vertex_color ? 1 : 0);
}

glm::mat4 panel_model(const glm::vec3 center, const float yaw_degrees, const glm::vec3 scale) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
    model = glm::rotate(model, glm::radians(yaw_degrees), glm::vec3(0.0f, 1.0f, 0.0f));
    return glm::scale(model, scale);
}

float axis_x_yaw_radians(const glm::vec3& axis) {
    glm::vec3 flat(axis.x, 0.0f, axis.z);
    if (glm::length(flat) <= 0.0001f) {
        flat = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
        flat = glm::normalize(flat);
    }
    return std::atan2(-flat.z, flat.x);
}

void draw_world_panel(shader_program& shader,
                      const glm::mat4& view,
                      const glm::mat4& proj,
                      const glm::vec3& center,
                      const glm::vec3& axis_x,
                      const float local_z_rotation,
                      const glm::vec2& half_size,
                      const glm::vec3& color) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
    model = glm::rotate(model, axis_x_yaw_radians(axis_x), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, local_z_rotation, glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, glm::vec3(half_size, 1.0f));
    set_terrain_draw_state(shader, model, view, proj, color, false);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void draw_swing_club(shader_program& shader,
                     const glm::mat4& view,
                     const glm::mat4& proj,
                     const render_data& data) {
    if (!data.shot_addressing && !data.swing_timing) {
        return;
    }

    const float power = std::clamp(data.swing_power, 0.0f, 1.0f);
    const glm::vec3 forward = aim_direction(data.aim_angle);
    glm::vec3 player_side = data.player_position - data.ball_position;
    player_side.y = 0.0f;
    if (glm::length(player_side) <= 0.0001f) {
        player_side = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward));
    } else {
        player_side = glm::normalize(player_side);
    }

    const float ball_radius = std::max(0.02f, data.ball_visual_radius_meters);
    const glm::vec3 head_center = data.ball_position
        + player_side * (ball_radius + 0.18f)
        - forward * 0.08f
        + glm::vec3(0.0f, ball_radius * 0.55f, 0.0f);

    const float backswing_angle = glm::radians(14.0f + power * 62.0f);
    const float shaft_length = 1.10f;
    const glm::vec3 shaft_direction = glm::normalize(player_side * std::sin(backswing_angle) +
                                                     glm::vec3(0.0f, std::cos(backswing_angle), 0.0f));
    const glm::vec3 shaft_center = head_center + shaft_direction * (shaft_length * 0.5f);

    draw_world_panel(shader,
                     view,
                     proj,
                     shaft_center,
                     player_side,
                     -backswing_angle,
                     glm::vec2(0.018f, shaft_length * 0.5f),
                     glm::vec3(0.82f, 0.78f, 0.62f));
    draw_world_panel(shader,
                     view,
                     proj,
                     head_center + forward * 0.03f,
                     forward,
                     0.0f,
                     glm::vec2(0.16f, 0.040f),
                     glm::vec3(0.16f, 0.15f, 0.13f));
    draw_world_panel(shader,
                     view,
                     proj,
                     head_center - player_side * 0.02f,
                     player_side,
                     0.0f,
                     glm::vec2(0.13f, 0.035f),
                     glm::vec3(0.09f, 0.085f, 0.075f));
}

const std::array<const char*, 7>& glyph_rows(const char value) {
    static const std::array<const char*, 7> glyph_a = {
        "0110",
        "1001",
        "1001",
        "1111",
        "1001",
        "1001",
        "1001"
    };
    static const std::array<const char*, 7> glyph_b = {
        "1110",
        "1001",
        "1001",
        "1110",
        "1001",
        "1001",
        "1110"
    };
    static const std::array<const char*, 7> glyph_g = {
        "0111",
        "1000",
        "1000",
        "1011",
        "1001",
        "1001",
        "0111"
    };
    static const std::array<const char*, 7> glyph_h = {
        "1001",
        "1001",
        "1001",
        "1111",
        "1001",
        "1001",
        "1001"
    };
    static const std::array<const char*, 7> glyph_l = {
        "1000",
        "1000",
        "1000",
        "1000",
        "1000",
        "1000",
        "1111"
    };
    static const std::array<const char*, 7> glyph_n = {
        "1001",
        "1101",
        "1101",
        "1011",
        "1011",
        "1001",
        "1001"
    };
    static const std::array<const char*, 7> glyph_p = {
        "1110",
        "1001",
        "1001",
        "1110",
        "1000",
        "1000",
        "1000"
    };
    static const std::array<const char*, 7> glyph_q = {
        "0110",
        "1001",
        "1001",
        "1001",
        "1011",
        "1001",
        "0111"
    };
    static const std::array<const char*, 7> glyph_o = {
        "0110",
        "1001",
        "1001",
        "1001",
        "1001",
        "1001",
        "0110"
    };
    static const std::array<const char*, 7> glyph_f = {
        "1111",
        "1000",
        "1000",
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
    static const std::array<const char*, 7> glyph_e = {
        "1111",
        "1000",
        "1000",
        "1110",
        "1000",
        "1000",
        "1111"
    };
    static const std::array<const char*, 7> glyph_r = {
        "1110",
        "1001",
        "1001",
        "1110",
        "1010",
        "1001",
        "1001"
    };
    static const std::array<const char*, 7> glyph_t = {
        "11111",
        "00100",
        "00100",
        "00100",
        "00100",
        "00100",
        "00100"
    };
    static const std::array<const char*, 7> glyph_u = {
        "1001",
        "1001",
        "1001",
        "1001",
        "1001",
        "1001",
        "0110"
    };
    static const std::array<const char*, 7> glyph_v = {
        "1001",
        "1001",
        "1001",
        "1001",
        "1001",
        "0110",
        "0110"
    };
    static const std::array<const char*, 7> glyph_y = {
        "1001",
        "1001",
        "0110",
        "0010",
        "0010",
        "0010",
        "0010"
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
    static const std::array<const char*, 7> glyph_d = {
        "1110",
        "1001",
        "1001",
        "1001",
        "1001",
        "1001",
        "1110"
    };
    static const std::array<const char*, 7> glyph_m = {
        "10001",
        "11011",
        "10101",
        "10101",
        "10001",
        "10001",
        "10001"
    };
    static const std::array<const char*, 7> glyph_0 = {
        "0110",
        "1001",
        "1001",
        "1001",
        "1001",
        "1001",
        "0110"
    };
    static const std::array<const char*, 7> glyph_1 = {
        "010",
        "110",
        "010",
        "010",
        "010",
        "010",
        "111"
    };
    static const std::array<const char*, 7> glyph_2 = {
        "1110",
        "0001",
        "0001",
        "0110",
        "1000",
        "1000",
        "1111"
    };
    static const std::array<const char*, 7> glyph_3 = {
        "1110",
        "0001",
        "0001",
        "0110",
        "0001",
        "0001",
        "1110"
    };
    static const std::array<const char*, 7> glyph_4 = {
        "1001",
        "1001",
        "1001",
        "1111",
        "0001",
        "0001",
        "0001"
    };
    static const std::array<const char*, 7> glyph_5 = {
        "1111",
        "1000",
        "1000",
        "1110",
        "0001",
        "0001",
        "1110"
    };
    static const std::array<const char*, 7> glyph_6 = {
        "0111",
        "1000",
        "1000",
        "1110",
        "1001",
        "1001",
        "0110"
    };
    static const std::array<const char*, 7> glyph_8 = {
        "0110",
        "1001",
        "1001",
        "0110",
        "1001",
        "1001",
        "0110"
    };
    static const std::array<const char*, 7> glyph_9 = {
        "0110",
        "1001",
        "1001",
        "0111",
        "0001",
        "0001",
        "1110"
    };

    switch (value) {
    case '0':
        return glyph_0;
    case '1':
        return glyph_1;
    case '2':
        return glyph_2;
    case '3':
        return glyph_3;
    case '4':
        return glyph_4;
    case '5':
        return glyph_5;
    case '6':
        return glyph_6;
    case '7':
        return glyph_7;
    case '8':
        return glyph_8;
    case '9':
        return glyph_9;
    case 'A':
        return glyph_a;
    case 'B':
        return glyph_b;
    case 'G':
        return glyph_g;
    case 'H':
        return glyph_h;
    case 'L':
        return glyph_l;
    case 'N':
        return glyph_n;
    case 'P':
        return glyph_p;
    case 'Q':
        return glyph_q;
    case 'O':
        return glyph_o;
    case 'F':
        return glyph_f;
    case 'W':
        return glyph_w;
    case 'E':
        return glyph_e;
    case 'R':
        return glyph_r;
    case 'T':
        return glyph_t;
    case 'U':
        return glyph_u;
    case 'V':
        return glyph_v;
    case 'Y':
        return glyph_y;
    case 'S':
        return glyph_s;
    case 'C':
        return glyph_c;
    case 'D':
        return glyph_d;
    case 'M':
    case 'm':
        return glyph_m;
    default:
        return glyph_i;
    }
}

int glyph_width(const char value) {
    if (value == ' ') {
        return 3;
    }
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
    return static_cast<int>(std::string(glyph_rows(upper)[0]).size());
}

void draw_overlay_quad(shader_program& shader,
                       const glm::vec2 center,
                       const glm::vec2 half_size,
                       const glm::vec3 color,
                       const float alpha = 1.0f) {
    const glm::mat4 model = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f)),
                                       glm::vec3(half_size, 1.0f));
    shader.set_mat4("u_model", glm::mat4(1.0f));
    shader.set_mat4("u_mvp", model);
    shader.set_vec3("u_color", color);
    shader.set_float("u_alpha", alpha);
    shader.set_int("u_use_vertex_color", 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void draw_overlay_rotated_quad(shader_program& shader,
                               const glm::vec2 center,
                               const glm::vec2 half_size,
                               const float angle_radians,
                               const glm::vec3 color,
                               const float alpha) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f));
    model = glm::rotate(model, angle_radians, glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, glm::vec3(half_size, 1.0f));
    shader.set_mat4("u_model", glm::mat4(1.0f));
    shader.set_mat4("u_mvp", model);
    shader.set_vec3("u_color", color);
    shader.set_float("u_alpha", alpha);
    shader.set_int("u_use_vertex_color", 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void draw_overlay_segment(shader_program& shader,
                          const glm::vec2 start,
                          const glm::vec2 end,
                          const float thickness,
                          const glm::vec3 color,
                          const float alpha) {
    const glm::vec2 delta = end - start;
    const float length = glm::length(delta);
    if (length <= 0.00001f) {
        return;
    }

    draw_overlay_rotated_quad(shader,
                              (start + end) * 0.5f,
                              glm::vec2(length * 0.5f, thickness * 0.5f),
                              std::atan2(delta.y, delta.x),
                              color,
                              alpha);
}

void draw_button_outline(shader_program& shader,
                         const glm::vec2 center,
                         const glm::vec2 half_size,
                         const glm::vec3 color,
                         const float alpha) {
    constexpr float line_thickness = 0.008f;
    const glm::vec2 top_left(center.x - half_size.x, center.y + half_size.y);
    const glm::vec2 top_right(center.x + half_size.x, center.y + half_size.y);
    const glm::vec2 bottom_left(center.x - half_size.x, center.y - half_size.y);
    const glm::vec2 bottom_right(center.x + half_size.x, center.y - half_size.y);

    draw_overlay_segment(shader, top_left, top_right, line_thickness, color, alpha);
    draw_overlay_segment(shader, top_right, bottom_right, line_thickness, color, alpha);
    draw_overlay_segment(shader, bottom_right, bottom_left, line_thickness, color, alpha);
    draw_overlay_segment(shader, bottom_left, top_left, line_thickness, color, alpha);
}

void draw_control_button_base(shader_program& shader,
                              const glm::vec2 center,
                              const glm::vec2 half_size,
                              const bool is_down) {
    const glm::vec3 outline_color(0.66f, 0.68f, 0.66f);
    const glm::vec3 pressed_color(0.88f, 0.70f, 0.30f);

    if (is_down) {
        draw_overlay_quad(shader, center, half_size, pressed_color, 0.90f);
    } else {
        draw_overlay_quad(shader, center, half_size, glm::vec3(0.12f, 0.13f, 0.13f), 0.08f);
    }

    draw_button_outline(shader, center, half_size, outline_color, is_down ? 0.95f : 0.58f);
}

glm::vec3 control_icon_color(const bool is_down) {
    return is_down ? glm::vec3(0.08f, 0.085f, 0.08f) : glm::vec3(0.70f, 0.72f, 0.70f);
}

float control_icon_alpha(const bool is_down) {
    return is_down ? 1.0f : 0.58f;
}

void draw_arrow_icon(shader_program& shader,
                     const glm::vec2 center,
                     const glm::vec2 direction,
                     const glm::vec2 half_size,
                     const bool is_down) {
    const glm::vec2 dir = glm::normalize(direction);
    const glm::vec2 side(-dir.y, dir.x);
    const float radius = std::min(half_size.x, half_size.y) * 0.66f;
    const float thickness = std::max(0.008f, radius * 0.14f);
    const glm::vec2 tip = center + dir * radius;
    const glm::vec2 tail = center - dir * (radius * 0.48f);
    const glm::vec2 shoulder = tip - dir * (radius * 0.46f);
    const glm::vec3 color = control_icon_color(is_down);
    const float alpha = control_icon_alpha(is_down);

    draw_overlay_segment(shader, tail, tip, thickness, color, alpha);
    draw_overlay_segment(shader, tip, shoulder + side * (radius * 0.34f), thickness, color, alpha);
    draw_overlay_segment(shader, tip, shoulder - side * (radius * 0.34f), thickness, color, alpha);
}

void draw_space_icon(shader_program& shader, const glm::vec2 center, const glm::vec2 half_size, const bool is_down) {
    const glm::vec3 color = control_icon_color(is_down);
    const float alpha = control_icon_alpha(is_down);
    const float thickness = 0.010f;
    const float width = half_size.x * 1.08f;
    const float height = half_size.y * 0.34f;
    const glm::vec2 left(center.x - width * 0.5f, center.y - height * 0.15f);
    const glm::vec2 right(center.x + width * 0.5f, center.y - height * 0.15f);

    draw_overlay_segment(shader, left, right, thickness, color, alpha);
    draw_overlay_segment(shader, left, left + glm::vec2(0.0f, height), thickness, color, alpha);
    draw_overlay_segment(shader, right, right + glm::vec2(0.0f, height), thickness, color, alpha);
}

void draw_shift_icon(shader_program& shader, const glm::vec2 center, const glm::vec2 half_size, const bool is_down) {
    const glm::vec3 color = control_icon_color(is_down);
    const float alpha = control_icon_alpha(is_down);
    const float thickness = 0.010f;
    const float width = half_size.x * 0.84f;
    const float height = half_size.y * 0.88f;
    const glm::vec2 tip = center + glm::vec2(0.0f, height * 0.48f);
    const glm::vec2 left_shoulder = center + glm::vec2(-width * 0.38f, height * 0.04f);
    const glm::vec2 right_shoulder = center + glm::vec2(width * 0.38f, height * 0.04f);
    const glm::vec2 left_base = center + glm::vec2(-width * 0.20f, -height * 0.48f);
    const glm::vec2 right_base = center + glm::vec2(width * 0.20f, -height * 0.48f);

    draw_overlay_segment(shader, tip, left_shoulder, thickness, color, alpha);
    draw_overlay_segment(shader, tip, right_shoulder, thickness, color, alpha);
    draw_overlay_segment(shader, left_shoulder, left_base, thickness, color, alpha);
    draw_overlay_segment(shader, right_shoulder, right_base, thickness, color, alpha);
    draw_overlay_segment(shader, left_base, right_base, thickness, color, alpha);
}

void draw_enter_icon(shader_program& shader, const glm::vec2 center, const glm::vec2 half_size, const bool is_down) {
    const glm::vec3 color = control_icon_color(is_down);
    const float alpha = control_icon_alpha(is_down);
    const float thickness = 0.010f;
    const glm::vec2 top = center + glm::vec2(half_size.x * 0.44f, half_size.y * 0.40f);
    const glm::vec2 turn = center + glm::vec2(half_size.x * 0.44f, -half_size.y * 0.10f);
    const glm::vec2 tip = center + glm::vec2(-half_size.x * 0.42f, -half_size.y * 0.10f);
    const glm::vec2 shoulder = tip + glm::vec2(half_size.x * 0.30f, 0.0f);

    draw_overlay_segment(shader, top, turn, thickness, color, alpha);
    draw_overlay_segment(shader, turn, tip, thickness, color, alpha);
    draw_overlay_segment(shader, tip, shoulder + glm::vec2(0.0f, half_size.y * 0.22f), thickness, color, alpha);
    draw_overlay_segment(shader, tip, shoulder - glm::vec2(0.0f, half_size.y * 0.22f), thickness, color, alpha);
}

void draw_backspace_icon(shader_program& shader, const glm::vec2 center, const glm::vec2 half_size, const bool is_down) {
    const glm::vec3 color = control_icon_color(is_down);
    const float alpha = control_icon_alpha(is_down);
    const float thickness = 0.010f;
    const glm::vec2 tip = center + glm::vec2(-half_size.x * 0.44f, 0.0f);
    const glm::vec2 mid = center + glm::vec2(half_size.x * 0.20f, 0.0f);

    draw_overlay_segment(shader, tip, mid, thickness, color, alpha);
    draw_overlay_segment(shader, tip, center + glm::vec2(-half_size.x * 0.10f, half_size.y * 0.28f), thickness, color, alpha);
    draw_overlay_segment(shader, tip, center + glm::vec2(-half_size.x * 0.10f, -half_size.y * 0.28f), thickness, color, alpha);
    draw_overlay_segment(shader,
                         center + glm::vec2(half_size.x * 0.36f, half_size.y * 0.34f),
                         center + glm::vec2(half_size.x * 0.36f, -half_size.y * 0.34f),
                         thickness,
                         color,
                         alpha);
}

void draw_retee_icon(shader_program& shader, const glm::vec2 center, const glm::vec2 half_size, const bool is_down) {
    const glm::vec3 color = control_icon_color(is_down);
    const float alpha = control_icon_alpha(is_down);
    const float radius = std::min(half_size.x, half_size.y) * 0.52f;
    const float thickness = 0.010f;
    constexpr int segment_count = 12;
    constexpr float start_angle = -0.45f;
    constexpr float end_angle = 4.65f;

    glm::vec2 previous = center + glm::vec2(std::cos(start_angle), std::sin(start_angle)) * radius;
    for (int i = 1; i <= segment_count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segment_count);
        const float angle = start_angle + (end_angle - start_angle) * t;
        const glm::vec2 next = center + glm::vec2(std::cos(angle), std::sin(angle)) * radius;
        draw_overlay_segment(shader, previous, next, thickness, color, alpha);
        previous = next;
    }

    const glm::vec2 tip = center + glm::vec2(std::cos(end_angle), std::sin(end_angle)) * radius;
    draw_overlay_segment(shader, tip, tip + glm::vec2(half_size.x * 0.18f, half_size.y * 0.08f), thickness, color, alpha);
    draw_overlay_segment(shader, tip, tip + glm::vec2(half_size.x * 0.04f, -half_size.y * 0.22f), thickness, color, alpha);
}

void draw_controls_overlay(shader_program& shader, const controls_overlay_state& controls) {
    if (!controls.visible) {
        return;
    }

    const glm::vec2 dpad_half(0.060f, 0.060f);
    const glm::vec2 small_half(0.066f, 0.052f);
    const glm::vec2 wide_half(0.138f, 0.052f);
    const glm::vec2 dpad_center(0.79f, 0.02f);

    draw_control_button_base(shader, dpad_center + glm::vec2(0.0f, 0.126f), dpad_half, controls.up_down);
    draw_arrow_icon(shader, dpad_center + glm::vec2(0.0f, 0.126f), glm::vec2(0.0f, 1.0f), dpad_half, controls.up_down);

    draw_control_button_base(shader, dpad_center + glm::vec2(-0.070f, 0.0f), dpad_half, controls.left_down);
    draw_arrow_icon(shader, dpad_center + glm::vec2(-0.070f, 0.0f), glm::vec2(-1.0f, 0.0f), dpad_half, controls.left_down);

    draw_control_button_base(shader, dpad_center + glm::vec2(0.070f, 0.0f), dpad_half, controls.right_down);
    draw_arrow_icon(shader, dpad_center + glm::vec2(0.070f, 0.0f), glm::vec2(1.0f, 0.0f), dpad_half, controls.right_down);

    draw_control_button_base(shader, dpad_center + glm::vec2(0.0f, -0.126f), dpad_half, controls.down_down);
    draw_arrow_icon(shader, dpad_center + glm::vec2(0.0f, -0.126f), glm::vec2(0.0f, -1.0f), dpad_half, controls.down_down);

    draw_control_button_base(shader, glm::vec2(0.79f, -0.32f), wide_half, controls.space_down);
    draw_space_icon(shader, glm::vec2(0.79f, -0.32f), wide_half, controls.space_down);

    draw_control_button_base(shader, glm::vec2(0.70f, -0.46f), small_half, controls.shift_down);
    draw_shift_icon(shader, glm::vec2(0.70f, -0.46f), small_half, controls.shift_down);

    draw_control_button_base(shader, glm::vec2(0.88f, -0.46f), small_half, controls.enter_down);
    draw_enter_icon(shader, glm::vec2(0.88f, -0.46f), small_half, controls.enter_down);

    draw_control_button_base(shader, glm::vec2(0.70f, -0.60f), small_half, controls.backspace_down);
    draw_backspace_icon(shader, glm::vec2(0.70f, -0.60f), small_half, controls.backspace_down);

    draw_control_button_base(shader, glm::vec2(0.88f, -0.60f), small_half, controls.retee_down);
    draw_retee_icon(shader, glm::vec2(0.88f, -0.60f), small_half, controls.retee_down);
}

void draw_pixel_glyph(shader_program& shader,
                      const char value,
                      const glm::vec2 top_left,
                      const float pixel_size,
                      const glm::vec3 color) {
    if (value == ' ') {
        return;
    }

    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
    const std::array<const char*, 7>& rows = glyph_rows(upper);
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

float pixel_text_width(const std::string& label, const float pixel_size) {
    float width = 0.0f;
    for (const char c : label) {
        width += static_cast<float>(glyph_width(c) + 1) * pixel_size;
    }
    return std::max(0.0f, width - pixel_size);
}

void draw_pixel_text_centered(shader_program& shader,
                              const std::string& label,
                              const glm::vec2 center,
                              const float pixel_size,
                              const glm::vec3 color) {
    const float width = pixel_text_width(label, pixel_size);
    glm::vec2 cursor(center.x - width * 0.5f, center.y + 3.5f * pixel_size);
    for (const char c : label) {
        draw_pixel_glyph(shader, c, cursor, pixel_size, color);
        cursor.x += static_cast<float>(glyph_width(c) + 1) * pixel_size;
    }
}

void draw_pixel_text_left(shader_program& shader,
                          const std::string& label,
                          const glm::vec2 top_left,
                          const float pixel_size,
                          const glm::vec3 color) {
    glm::vec2 cursor = top_left;
    for (const char c : label) {
        draw_pixel_glyph(shader, c, cursor, pixel_size, color);
        cursor.x += static_cast<float>(glyph_width(c) + 1) * pixel_size;
    }
}

void draw_fps_counter(shader_program& shader, const std::string& label) {
    if (label.empty()) {
        return;
    }

    draw_pixel_text_left(shader, label, glm::vec2(-0.96f, 0.92f), 0.018f, glm::vec3(0.96f, 0.78f, 0.18f));
}

void draw_club_label(shader_program& shader, const std::string& label) {
    const glm::vec3 label_color(0.90f, 0.88f, 0.76f);
    const glm::vec3 panel_color(0.055f, 0.06f, 0.07f);
    draw_overlay_quad(shader, glm::vec2(0.78f, 0.78f), glm::vec2(0.17f, 0.12f), panel_color);

    const float pixel_size = 0.025f;
    const float width = pixel_text_width(label, pixel_size);

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
    const float width = pixel_text_width(label, pixel_size);

    glm::vec2 cursor(-width * 0.5f, -0.51f);
    for (const char c : label) {
        draw_pixel_glyph(shader, c, cursor, pixel_size, prompt_color);
        cursor.x += static_cast<float>(glyph_width(c) + 1) * pixel_size;
    }
}

void draw_power_meter(shader_program& shader, const float swing_power) {
    const float power = std::clamp(swing_power, 0.0f, 1.0f);
    const glm::vec3 panel_color(0.055f, 0.060f, 0.065f);
    const glm::vec3 outline_color(0.62f, 0.64f, 0.60f);
    const glm::vec3 text_color(0.88f, 0.86f, 0.72f);
    const glm::vec3 amber(0.92f, 0.70f, 0.18f);

    const glm::vec2 panel_center(-0.62f, -0.72f);
    const glm::vec2 panel_half(0.32f, 0.155f);
    draw_overlay_quad(shader, panel_center + glm::vec2(0.012f, -0.014f), panel_half, glm::vec3(0.0f), 0.22f);
    draw_overlay_quad(shader, panel_center, panel_half, panel_color, 0.78f);
    draw_button_outline(shader, panel_center, panel_half, outline_color, 0.54f);

    draw_pixel_text_left(shader, "POWER", panel_center + glm::vec2(-0.275f, 0.105f), 0.015f, text_color);

    const glm::vec2 track_center = panel_center + glm::vec2(0.020f, -0.012f);
    const glm::vec2 track_half(0.245f, 0.035f);
    const float track_left = track_center.x - track_half.x;
    const float track_right = track_center.x + track_half.x;
    const float track_width = track_half.x * 2.0f;

    draw_overlay_quad(shader, track_center, track_half, glm::vec3(0.030f, 0.032f, 0.034f), 0.92f);
    draw_button_outline(shader, track_center, track_half, outline_color, 0.56f);

    if (power > 0.0f) {
        const float fill_half_width = track_half.x * power;
        const glm::vec2 fill_center(track_left + fill_half_width, track_center.y);
        draw_overlay_quad(shader, fill_center, glm::vec2(fill_half_width, track_half.y * 0.58f), amber, 0.92f);
    }

    const float sweet_x = track_left + track_width * 0.86f;
    draw_overlay_segment(shader,
                         glm::vec2(sweet_x, track_center.y - track_half.y * 1.08f),
                         glm::vec2(sweet_x, track_center.y + track_half.y * 1.08f),
                         0.006f,
                         glm::vec3(0.96f, 0.88f, 0.55f),
                         0.88f);

    const float needle_x = track_left + track_width * power;
    draw_overlay_segment(shader,
                         glm::vec2(needle_x, track_center.y - 0.055f),
                         glm::vec2(needle_x, track_center.y + 0.055f),
                         0.008f,
                         glm::vec3(0.98f, 0.80f, 0.24f),
                         0.96f);

    const std::array<float, 3> ticks{0.0f, 0.5f, 1.0f};
    for (const float tick : ticks) {
        const float x = track_left + track_width * tick;
        draw_overlay_segment(shader,
                             glm::vec2(x, track_center.y - 0.058f),
                             glm::vec2(x, track_center.y - 0.080f),
                             0.005f,
                             outline_color,
                             0.72f);
    }

    draw_pixel_text_centered(shader, "0", glm::vec2(track_left, panel_center.y - 0.112f), 0.011f, text_color);
    draw_pixel_text_centered(shader, "50", glm::vec2(track_center.x, panel_center.y - 0.112f), 0.011f, text_color);
    draw_pixel_text_centered(shader, "100", glm::vec2(track_right, panel_center.y - 0.112f), 0.011f, text_color);
}

glm::vec3 thumbnail_zone_color(const material_zone_type type) {
    switch (type) {
    case material_zone_type::green:
        return glm::vec3(0.20f, 0.62f, 0.24f);
    case material_zone_type::bunker:
        return glm::vec3(0.68f, 0.56f, 0.24f);
    case material_zone_type::water:
        return glm::vec3(0.12f, 0.24f, 0.66f);
    default:
        return glm::vec3(0.28f, 0.30f, 0.28f);
    }
}

glm::vec2 thumbnail_world_point(const glm::vec3& point, const bool rotate_long_axis) {
    if (rotate_long_axis) {
        return glm::vec2(point.z, -point.x);
    }
    return glm::vec2(point.x, point.z);
}

void expand_preview_bounds(const glm::vec2& point, glm::vec2& min_point, glm::vec2& max_point) {
    min_point.x = std::min(min_point.x, point.x);
    min_point.y = std::min(min_point.y, point.y);
    max_point.x = std::max(max_point.x, point.x);
    max_point.y = std::max(max_point.y, point.y);
}

glm::vec2 preview_point(const glm::vec3& point,
                        const glm::vec2& center,
                        const glm::vec2& half_size,
                        const glm::vec2& min_point,
                        const float scale,
                        const bool rotate_long_axis) {
    const glm::vec2 world = thumbnail_world_point(point, rotate_long_axis);
    return center + glm::vec2((world.x - min_point.x) * scale - half_size.x,
                              (world.y - min_point.y) * scale - half_size.y);
}

void draw_hole_thumbnail(shader_program& shader,
                         const render_hole_preview& preview,
                         const glm::vec2 center,
                         const glm::vec2 half_size) {
    draw_overlay_quad(shader, center, half_size, glm::vec3(0.028f, 0.034f, 0.030f), 0.96f);
    draw_button_outline(shader, center, half_size, glm::vec3(0.42f, 0.44f, 0.40f), 0.42f);

    glm::vec2 raw_min(preview.tee_position.x, preview.tee_position.z);
    glm::vec2 raw_max = raw_min;
    expand_preview_bounds(glm::vec2(preview.pin_position.x, preview.pin_position.z), raw_min, raw_max);
    for (const glm::vec3& point : preview.control_points) {
        expand_preview_bounds(glm::vec2(point.x, point.z), raw_min, raw_max);
    }
    for (const material_zone& zone : preview.material_zones) {
        if (zone.has_radius) {
            expand_preview_bounds(glm::vec2(zone.center.x + zone.radius, zone.center.z + zone.radius), raw_min, raw_max);
            expand_preview_bounds(glm::vec2(zone.center.x - zone.radius, zone.center.z - zone.radius), raw_min, raw_max);
        }
        if (zone.has_bounds) {
            expand_preview_bounds(glm::vec2(zone.bounds_min.x, zone.bounds_min.z), raw_min, raw_max);
            expand_preview_bounds(glm::vec2(zone.bounds_max.x, zone.bounds_max.z), raw_min, raw_max);
        }
    }

    const bool rotate_long_axis = (raw_max.y - raw_min.y) > (raw_max.x - raw_min.x);
    glm::vec2 min_point = thumbnail_world_point(preview.tee_position, rotate_long_axis);
    glm::vec2 max_point = min_point;
    expand_preview_bounds(thumbnail_world_point(preview.pin_position, rotate_long_axis), min_point, max_point);
    for (const glm::vec3& point : preview.control_points) {
        expand_preview_bounds(thumbnail_world_point(point, rotate_long_axis), min_point, max_point);
    }
    for (const material_zone& zone : preview.material_zones) {
        if (zone.has_radius) {
            expand_preview_bounds(thumbnail_world_point(zone.center + glm::vec3(zone.radius, 0.0f, zone.radius), rotate_long_axis), min_point, max_point);
            expand_preview_bounds(thumbnail_world_point(zone.center - glm::vec3(zone.radius, 0.0f, zone.radius), rotate_long_axis), min_point, max_point);
        }
        if (zone.has_bounds) {
            expand_preview_bounds(thumbnail_world_point(zone.bounds_min, rotate_long_axis), min_point, max_point);
            expand_preview_bounds(thumbnail_world_point(zone.bounds_max, rotate_long_axis), min_point, max_point);
        }
    }

    const glm::vec2 span = glm::max(max_point - min_point, glm::vec2(1.0f));
    const glm::vec2 inset_half = half_size * 0.82f;
    const float scale = std::min((inset_half.x * 2.0f) / span.x, (inset_half.y * 2.0f) / span.y);
    const glm::vec2 padded_min = min_point - (glm::vec2(inset_half.x * 2.0f, inset_half.y * 2.0f) / scale - span) * 0.5f;

    for (const material_zone& zone : preview.material_zones) {
        const glm::vec3 color = thumbnail_zone_color(zone.type);
        if (zone.has_bounds) {
            const glm::vec2 a = preview_point(zone.bounds_min, center, inset_half, padded_min, scale, rotate_long_axis);
            const glm::vec2 b = preview_point(zone.bounds_max, center, inset_half, padded_min, scale, rotate_long_axis);
            draw_overlay_quad(shader, (a + b) * 0.5f, glm::abs(b - a) * 0.5f, color, 0.64f);
        } else if (zone.has_radius) {
            const glm::vec2 p = preview_point(zone.center, center, inset_half, padded_min, scale, rotate_long_axis);
            const float radius = std::max(0.010f, zone.radius * scale);
            draw_overlay_quad(shader, p, glm::vec2(radius), color, 0.64f);
        }
    }

    if (preview.control_points.size() >= 2) {
        const float fairway_width = std::max(0.010f, preview.fairway_width * scale * 0.35f);
        for (std::size_t i = 1; i < preview.control_points.size(); ++i) {
            const glm::vec2 a = preview_point(preview.control_points[i - 1], center, inset_half, padded_min, scale, rotate_long_axis);
            const glm::vec2 b = preview_point(preview.control_points[i], center, inset_half, padded_min, scale, rotate_long_axis);
            draw_overlay_segment(shader, a, b, fairway_width, glm::vec3(0.18f, 0.46f, 0.18f), 0.82f);
            draw_overlay_segment(shader, a, b, 0.006f, glm::vec3(0.62f, 0.78f, 0.38f), 0.55f);
        }
    }

    const glm::vec2 tee = preview_point(preview.tee_position, center, inset_half, padded_min, scale, rotate_long_axis);
    const glm::vec2 pin = preview_point(preview.pin_position, center, inset_half, padded_min, scale, rotate_long_axis);
    draw_overlay_quad(shader, tee, glm::vec2(0.014f), glm::vec3(0.88f, 0.80f, 0.48f), 0.94f);
    draw_overlay_quad(shader, pin, glm::vec2(0.012f, 0.028f), glm::vec3(0.88f, 0.18f, 0.12f), 0.94f);
}

glm::vec2 startup_tile_center(const startup_menu_screen screen, const int index) {
    if (screen == startup_menu_screen::main) {
        return glm::vec2(0.0f, 0.26f - static_cast<float>(index) * 0.24f);
    }

    constexpr int columns = 3;
    const int row = index / columns;
    const int column = index % columns;
    return glm::vec2(-0.58f + static_cast<float>(column) * 0.58f,
                     0.36f - static_cast<float>(row) * 0.38f);
}

glm::vec2 startup_tile_half_size(const startup_menu_screen screen) {
    return screen == startup_menu_screen::main ? glm::vec2(0.42f, 0.095f) : glm::vec2(0.25f, 0.165f);
}

void draw_startup_menu(shader_program& shader, const render_startup_menu& menu) {
    if (menu.screen == startup_menu_screen::none) {
        return;
    }

    draw_overlay_quad(shader, glm::vec2(0.0f), glm::vec2(1.0f), glm::vec3(0.0f, 0.0f, 0.0f), 0.72f);
    draw_overlay_quad(shader, glm::vec2(0.0f, 0.0f), glm::vec2(0.86f, 0.88f), glm::vec3(0.012f, 0.014f, 0.014f), 0.30f);
    draw_pixel_text_centered(shader, menu.title, glm::vec2(0.0f, 0.80f), 0.028f, glm::vec3(0.95f, 0.78f, 0.28f));
    if (!menu.subtitle.empty()) {
        draw_pixel_text_centered(shader, menu.subtitle, glm::vec2(0.0f, 0.68f), 0.015f, glm::vec3(0.72f, 0.72f, 0.62f));
    }

    const glm::vec2 tile_half = startup_tile_half_size(menu.screen);
    for (std::size_t i = 0; i < menu.tiles.size(); ++i) {
        const render_startup_tile& tile = menu.tiles[i];
        const glm::vec2 center = startup_tile_center(menu.screen, static_cast<int>(i));
        const glm::vec3 panel_color = tile.selected ? glm::vec3(0.20f, 0.16f, 0.065f) : glm::vec3(0.070f, 0.075f, 0.075f);
        const glm::vec3 outline = tile.selected ? glm::vec3(0.94f, 0.72f, 0.22f) : glm::vec3(0.50f, 0.52f, 0.48f);
        draw_overlay_quad(shader, center, tile_half, panel_color, tile.selected ? 0.94f : 0.76f);
        draw_button_outline(shader, center, tile_half, outline, tile.selected ? 0.94f : 0.44f);

        if (tile.has_preview) {
            draw_hole_thumbnail(shader, tile.preview, center + glm::vec2(0.0f, 0.030f), glm::vec2(tile_half.x * 0.86f, tile_half.y * 0.48f));
            draw_pixel_text_left(shader, tile.title, center + glm::vec2(-tile_half.x * 0.86f, -tile_half.y * 0.28f), 0.012f, glm::vec3(0.90f, 0.88f, 0.76f));
            draw_pixel_text_left(shader, tile.subtitle, center + glm::vec2(-tile_half.x * 0.86f, -tile_half.y * 0.58f), 0.010f, glm::vec3(0.68f, 0.69f, 0.62f));
        } else {
            draw_pixel_text_centered(shader, tile.title, center + glm::vec2(0.0f, 0.018f), 0.020f, glm::vec3(0.90f, 0.88f, 0.76f));
            draw_pixel_text_centered(shader, tile.subtitle, center + glm::vec2(0.0f, -0.050f), 0.011f, glm::vec3(0.66f, 0.67f, 0.61f));
        }
    }

    if (!menu.footer.empty()) {
        draw_pixel_text_centered(shader, menu.footer, glm::vec2(0.0f, -0.86f), 0.013f, glm::vec3(0.56f, 0.57f, 0.52f));
    }
}

bool project_to_screen(const glm::mat4& view,
                       const glm::mat4& proj,
                       const glm::vec3& world_position,
                       glm::vec2& screen_position) {
    const glm::vec4 clip = proj * view * glm::vec4(world_position, 1.0f);
    if (clip.w <= 0.0f) {
        return false;
    }

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < -1.0f || ndc.z > 1.0f) {
        return false;
    }

    screen_position = glm::vec2(ndc.x, ndc.y);
    return true;
}

void draw_rangefinder_view(shader_program& shader,
                           const glm::mat4& view,
                           const glm::mat4& proj,
                           const render_data& data) {
    draw_overlay_quad(shader, glm::vec2(0.0f), glm::vec2(1.0f), glm::vec3(0.16f, 0.34f, 0.24f), 0.18f);
    draw_overlay_quad(shader, glm::vec2(-0.91f, 0.0f), glm::vec2(0.18f, 1.0f), glm::vec3(0.01f, 0.018f, 0.014f), 0.52f);
    draw_overlay_quad(shader, glm::vec2(0.91f, 0.0f), glm::vec2(0.18f, 1.0f), glm::vec3(0.01f, 0.018f, 0.014f), 0.52f);
    draw_overlay_quad(shader, glm::vec2(0.0f, 0.89f), glm::vec2(1.0f, 0.22f), glm::vec3(0.01f, 0.018f, 0.014f), 0.45f);
    draw_overlay_quad(shader, glm::vec2(0.0f, -0.89f), glm::vec2(1.0f, 0.22f), glm::vec3(0.01f, 0.018f, 0.014f), 0.45f);

    const glm::vec3 reticle_color(0.62f, 0.96f, 0.64f);
    draw_overlay_quad(shader, glm::vec2(0.0f, 0.0f), glm::vec2(0.006f, 0.11f), reticle_color, 0.72f);
    draw_overlay_quad(shader, glm::vec2(0.0f, 0.0f), glm::vec2(0.11f, 0.006f), reticle_color, 0.72f);
    draw_overlay_quad(shader, glm::vec2(-0.18f, 0.0f), glm::vec2(0.055f, 0.006f), reticle_color, 0.56f);
    draw_overlay_quad(shader, glm::vec2(0.18f, 0.0f), glm::vec2(0.055f, 0.006f), reticle_color, 0.56f);
    draw_overlay_quad(shader, glm::vec2(0.0f, -0.18f), glm::vec2(0.006f, 0.055f), reticle_color, 0.56f);
    draw_overlay_quad(shader, glm::vec2(0.0f, 0.18f), glm::vec2(0.006f, 0.055f), reticle_color, 0.56f);

    glm::vec2 pin_screen(0.0f);
    const glm::vec3 label_anchor = data.pin_position + glm::vec3(0.0f, data.pin_visual_height_meters + 0.36f, 0.0f);
    if (!project_to_screen(view, proj, label_anchor, pin_screen)) {
        return;
    }

    pin_screen.x = std::max(-0.78f, std::min(0.78f, pin_screen.x));
    pin_screen.y = std::max(-0.68f, std::min(0.82f, pin_screen.y));
    const float pixel_size = 0.025f;
    const float label_width = pixel_text_width(data.rangefinder_distance_label, pixel_size);
    const glm::vec2 panel_half(std::max(0.13f, label_width * 0.5f + 0.045f), 0.085f);
    draw_overlay_quad(shader, pin_screen + glm::vec2(0.0f, 0.015f), panel_half, glm::vec3(0.015f, 0.032f, 0.024f), 0.78f);
    draw_pixel_text_centered(shader,
                             data.rangefinder_distance_label,
                             pin_screen + glm::vec2(0.0f, 0.015f),
                             pixel_size,
                             glm::vec3(0.76f, 1.0f, 0.72f));
}

struct course_map_layout {
    glm::vec2 center{0.0f, 0.02f};
    glm::vec2 half_size{0.68f, 0.76f};
    glm::vec3 world_center{0.0f};
    float scale = 0.01f;
};

void expand_map_bounds(const glm::vec3& position, glm::vec2& min_point, glm::vec2& max_point) {
    min_point.x = std::min(min_point.x, position.x);
    min_point.y = std::min(min_point.y, position.z);
    max_point.x = std::max(max_point.x, position.x);
    max_point.y = std::max(max_point.y, position.z);
}

course_map_layout make_course_map_layout(const render_data& data) {
    glm::vec2 min_point(data.tee_position.x, data.tee_position.z);
    glm::vec2 max_point = min_point;
    expand_map_bounds(data.pin_position, min_point, max_point);
    expand_map_bounds(data.player_position, min_point, max_point);
    expand_map_bounds(data.ball_position, min_point, max_point);

    for (const render_terrain_vertex& vertex : data.terrain_vertices) {
        expand_map_bounds(vertex.position, min_point, max_point);
    }

    for (const render_tree& tree : data.trees) {
        expand_map_bounds(tree.base, min_point, max_point);
        expand_map_bounds(tree.base + glm::vec3(tree.leaf_radius, 0.0f, tree.leaf_radius), min_point, max_point);
        expand_map_bounds(tree.base - glm::vec3(tree.leaf_radius, 0.0f, tree.leaf_radius), min_point, max_point);
    }

    const glm::vec2 size = max_point - min_point;
    const float fallback_span = std::max(1.0f, data.course_extent);
    const float span = std::max(fallback_span * 0.12f, std::max(size.x, size.y));

    course_map_layout layout;
    layout.world_center = glm::vec3((min_point.x + max_point.x) * 0.5f,
                                    0.0f,
                                    (min_point.y + max_point.y) * 0.5f);
    layout.scale = std::min(layout.half_size.x, layout.half_size.y) * 1.72f / std::max(1.0f, span);
    return layout;
}

glm::vec2 map_point(const course_map_layout& layout, const glm::vec3& position) {
    // Paper map reads from the player's perspective, so world +X maps left.
    const glm::vec2 delta(layout.world_center.x - position.x, position.z - layout.world_center.z);
    return layout.center + delta * layout.scale;
}

void draw_map_marker(shader_program& shader,
                     const glm::vec2 position,
                     const glm::vec3 color,
                     const float radius) {
    draw_overlay_quad(shader, position, glm::vec2(radius), glm::vec3(0.04f, 0.035f, 0.025f), 0.55f);
    draw_overlay_quad(shader, position, glm::vec2(radius * 0.64f), color, 1.0f);
}

void add_triangle_scan_intersection(const glm::vec2 a,
                                    const glm::vec2 b,
                                    const float y,
                                    std::array<float, 3>& intersections,
                                    int& intersection_count) {
    const float min_y = std::min(a.y, b.y);
    const float max_y = std::max(a.y, b.y);
    if (std::abs(a.y - b.y) < 0.00001f || y < min_y || y >= max_y || intersection_count >= 3) {
        return;
    }

    const float t = (y - a.y) / (b.y - a.y);
    intersections[static_cast<std::size_t>(intersection_count)] = a.x + (b.x - a.x) * t;
    ++intersection_count;
}

void draw_filled_map_triangle(shader_program& shader,
                              const course_map_layout& layout,
                              const glm::vec2 a,
                              const glm::vec2 b,
                              const glm::vec2 c,
                              const glm::vec3 color) {
    const float inset = 0.025f;
    const glm::vec2 clip_min = layout.center - layout.half_size + glm::vec2(inset);
    const glm::vec2 clip_max = layout.center + layout.half_size - glm::vec2(inset);

    const float min_x = std::min({a.x, b.x, c.x});
    const float max_x = std::max({a.x, b.x, c.x});
    const float min_y = std::min({a.y, b.y, c.y});
    const float max_y = std::max({a.y, b.y, c.y});
    if (max_x < clip_min.x || min_x > clip_max.x || max_y < clip_min.y || min_y > clip_max.y) {
        return;
    }

    const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::abs(area) < 0.000001f) {
        return;
    }

    const float strip_height = std::max(0.0045f, std::min(0.011f, layout.scale * 0.72f));
    const float y_start = std::max(min_y, clip_min.y);
    const float y_end = std::min(max_y, clip_max.y);
    const int first_strip = static_cast<int>(std::floor((y_start - clip_min.y) / strip_height));
    const int last_strip = static_cast<int>(std::ceil((y_end - clip_min.y) / strip_height));

    for (int strip = first_strip; strip < last_strip; ++strip) {
        const float y = clip_min.y + (static_cast<float>(strip) + 0.5f) * strip_height;
        if (y < y_start || y > y_end) {
            continue;
        }

        std::array<float, 3> intersections{};
        int intersection_count = 0;
        add_triangle_scan_intersection(a, b, y, intersections, intersection_count);
        add_triangle_scan_intersection(b, c, y, intersections, intersection_count);
        add_triangle_scan_intersection(c, a, y, intersections, intersection_count);
        if (intersection_count < 2) {
            continue;
        }

        std::sort(intersections.begin(), intersections.begin() + intersection_count);
        const float x0 = std::max(intersections[0], clip_min.x);
        const float x1 = std::min(intersections[static_cast<std::size_t>(intersection_count - 1)], clip_max.x);
        if (x1 <= x0) {
            continue;
        }

        draw_overlay_quad(shader,
                          glm::vec2((x0 + x1) * 0.5f, y),
                          glm::vec2((x1 - x0) * 0.5f, strip_height * 0.56f),
                          color,
                          0.82f);
    }
}

void draw_filled_map_terrain(shader_program& shader, const course_map_layout& layout, const render_data& data) {
    if (data.terrain_vertices.empty() || data.terrain_indices.size() < 3) {
        return;
    }

    for (std::size_t i = 0; i + 2 < data.terrain_indices.size(); i += 3) {
        const std::uint32_t ia = data.terrain_indices[i];
        const std::uint32_t ib = data.terrain_indices[i + 1];
        const std::uint32_t ic = data.terrain_indices[i + 2];
        if (ia >= data.terrain_vertices.size() ||
            ib >= data.terrain_vertices.size() ||
            ic >= data.terrain_vertices.size()) {
            continue;
        }

        const render_terrain_vertex& va = data.terrain_vertices[ia];
        const render_terrain_vertex& vb = data.terrain_vertices[ib];
        const render_terrain_vertex& vc = data.terrain_vertices[ic];
        const glm::vec3 average_color = (va.color + vb.color + vc.color) / 3.0f;
        const glm::vec3 ink = average_color * 0.72f + glm::vec3(0.10f, 0.08f, 0.04f);
        draw_filled_map_triangle(shader,
                                 layout,
                                 map_point(layout, va.position),
                                 map_point(layout, vb.position),
                                 map_point(layout, vc.position),
                                 ink);
    }
}

void draw_paper_course_map(shader_program& shader, const render_data& data) {
    const course_map_layout layout = make_course_map_layout(data);
    const glm::vec3 paper(0.76f, 0.72f, 0.55f);
    const glm::vec3 paper_shadow(0.14f, 0.12f, 0.09f);
    draw_overlay_quad(shader, layout.center + glm::vec2(0.035f, -0.035f), layout.half_size, paper_shadow, 0.42f);
    draw_overlay_quad(shader, layout.center, layout.half_size, paper, 0.96f);

    const glm::vec3 fold(0.42f, 0.35f, 0.24f);
    draw_overlay_quad(shader, layout.center, glm::vec2(layout.half_size.x, 0.006f), fold, 0.18f);
    draw_overlay_quad(shader, layout.center, glm::vec2(0.006f, layout.half_size.y), fold, 0.18f);
    draw_overlay_quad(shader,
                      layout.center + glm::vec2(0.0f, layout.half_size.y),
                      glm::vec2(layout.half_size.x, 0.012f),
                      fold,
                      0.46f);
    draw_overlay_quad(shader,
                      layout.center - glm::vec2(0.0f, layout.half_size.y),
                      glm::vec2(layout.half_size.x, 0.012f),
                      fold,
                      0.46f);
    draw_overlay_quad(shader,
                      layout.center + glm::vec2(layout.half_size.x, 0.0f),
                      glm::vec2(0.012f, layout.half_size.y),
                      fold,
                      0.46f);
    draw_overlay_quad(shader,
                      layout.center - glm::vec2(layout.half_size.x, 0.0f),
                      glm::vec2(0.012f, layout.half_size.y),
                      fold,
                      0.46f);

    draw_filled_map_terrain(shader, layout, data);

    for (const render_tree& tree : data.trees) {
        const glm::vec2 p = map_point(layout, tree.base);
        const float radius = std::max(0.012f, std::min(0.028f, tree.leaf_radius * layout.scale));
        draw_map_marker(shader, p, glm::vec3(0.08f, 0.24f, 0.11f), radius);
    }

    draw_map_marker(shader, map_point(layout, data.tee_position), glm::vec3(0.34f, 0.21f, 0.12f), 0.023f);
    draw_map_marker(shader, map_point(layout, data.ball_position), glm::vec3(0.94f, 0.93f, 0.82f), 0.020f);
    draw_map_marker(shader, map_point(layout, data.player_position), glm::vec3(0.22f, 0.46f, 0.72f), 0.024f);

    const glm::vec2 pin = map_point(layout, data.pin_position);
    draw_overlay_quad(shader, pin + glm::vec2(0.0f, 0.028f), glm::vec2(0.004f, 0.042f), glm::vec3(0.06f, 0.04f, 0.025f), 0.92f);
    draw_overlay_quad(shader, pin + glm::vec2(0.022f, 0.055f), glm::vec2(0.028f, 0.018f), glm::vec3(0.76f, 0.17f, 0.12f), 0.96f);
    draw_map_marker(shader, pin, glm::vec3(0.94f, 0.78f, 0.22f), 0.018f);
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

    if (cylinder_vbo_ != 0) {
        glDeleteBuffers(1, &cylinder_vbo_);
        cylinder_vbo_ = 0;
    }

    if (cylinder_vao_ != 0) {
        glDeleteVertexArrays(1, &cylinder_vao_);
        cylinder_vao_ = 0;
    }

    if (cone_vbo_ != 0) {
        glDeleteBuffers(1, &cone_vbo_);
        cone_vbo_ = 0;
    }

    if (cone_vao_ != 0) {
        glDeleteVertexArrays(1, &cone_vao_);
        cone_vao_ = 0;
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
    render_overlay(view, proj, data);

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

    const std::vector<float> cylinder_vertices = make_cylinder_vertices(8);
    cylinder_vertex_count_ = static_cast<int>(cylinder_vertices.size() / 6);

    glGenVertexArrays(1, &cylinder_vao_);
    glGenBuffers(1, &cylinder_vbo_);
    glBindVertexArray(cylinder_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, cylinder_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(cylinder_vertices.size() * sizeof(float)),
                 cylinder_vertices.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);

    const std::vector<float> cone_vertices = make_cone_vertices(10);
    cone_vertex_count_ = static_cast<int>(cone_vertices.size() / 6);

    glGenVertexArrays(1, &cone_vao_);
    glGenBuffers(1, &cone_vbo_);
    glBindVertexArray(cone_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, cone_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(cone_vertices.size() * sizeof(float)),
                 cone_vertices.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
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

    for (const render_tree& tree : data.trees) {
        const float trunk_radius = std::max(0.01f, tree.trunk_radius);
        const float trunk_height = std::max(0.01f, tree.trunk_height);
        const float leaf_radius = std::max(0.01f, tree.leaf_radius);
        const float leaf_height = std::max(0.01f, tree.leaf_height);

        const glm::mat4 trunk_model = glm::scale(glm::translate(glm::mat4(1.0f), tree.base),
                                                 glm::vec3(trunk_radius, trunk_height, trunk_radius));
        set_terrain_draw_state(terrain_shader_, trunk_model, view, proj, glm::vec3(0.31f, 0.20f, 0.11f), false);
        glBindVertexArray(cylinder_vao_);
        glDrawArrays(GL_TRIANGLES, 0, cylinder_vertex_count_);

        const glm::mat4 leaf_model = glm::scale(glm::translate(glm::mat4(1.0f),
                                                               tree.base + glm::vec3(0.0f, trunk_height, 0.0f)),
                                                glm::vec3(leaf_radius, leaf_height, leaf_radius));
        set_terrain_draw_state(terrain_shader_, leaf_model, view, proj, glm::vec3(0.06f, 0.24f, 0.11f), false);
        glBindVertexArray(cone_vao_);
        glDrawArrays(GL_TRIANGLES, 0, cone_vertex_count_);
    }
    glBindVertexArray(0);

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

    if (data.shot_addressing || data.swing_timing) {
        terrain_shader_.use();
        terrain_shader_.set_vec3("u_light_dir", glm::normalize(glm::vec3(-0.35f, 0.80f, 0.42f)));
        glBindVertexArray(screen_vao_);
        draw_swing_club(terrain_shader_, view, proj, data);
        glBindVertexArray(0);
    }

    const float ball_radius = std::max(0.02f, data.ball_visual_radius_meters);
    const glm::mat4 ball_model = glm::scale(glm::translate(glm::mat4(1.0f),
                                                           data.ball_position),
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

void renderer::render_overlay(const glm::mat4& view, const glm::mat4& proj, const render_data& data) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    terrain_shader_.use();
    terrain_shader_.set_mat4("u_model", glm::mat4(1.0f));
    terrain_shader_.set_int("u_use_vertex_color", 0);
    terrain_shader_.set_float("u_alpha", 1.0f);
    terrain_shader_.set_vec3("u_light_dir", glm::vec3(0.0f, 1.0f, 0.0f));
    glBindVertexArray(screen_vao_);

    if (data.show_course_map) {
        draw_paper_course_map(terrain_shader_, data);
    }

    if (data.show_fps) {
        draw_fps_counter(terrain_shader_, data.fps_label);
    }

    if (data.show_rangefinder) {
        draw_rangefinder_view(terrain_shader_, view, proj, data);
    }

    draw_club_label(terrain_shader_, data.selected_club_label);

    if (data.show_interact_prompt) {
        draw_interact_prompt(terrain_shader_);
    }

    draw_controls_overlay(terrain_shader_, data.controls);

    if (data.swing_timing) {
        draw_power_meter(terrain_shader_, data.swing_power);
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

    draw_startup_menu(terrain_shader_, data.startup_menu);

    glBindVertexArray(0);
    glDisable(GL_BLEND);
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
