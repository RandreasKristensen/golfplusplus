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
#include <cstdint>
#include <string>
#include <vector>

#include "core/gl_loader.h"
#include "renderer/overlay_batch.h"
#include "renderer/overlay_pass.h"
#include "renderer/pixel_font.h"

namespace {
// All GL draw submissions go through these so draw calls are counted from the
// same non-global profiling sink the shader carries. Uniform sets are counted
// inside shader_program itself.
void draw_arrays(const shader_program& shader, const GLenum mode, const GLint first, const GLsizei count) {
    glDrawArrays(mode, first, count);
    record_draw_call(shader.profile());
}

void draw_elements(const shader_program& shader,
                   const GLenum mode,
                   const GLsizei count,
                   const GLenum type,
                   const void* offset) {
    glDrawElements(mode, count, type, offset);
    record_draw_call(shader.profile());
}

// Frustum-culled chunk ranges are merged into at most this many glDrawElements
// calls per mesh; culled chunks inside a merged span are drawn again rather
// than costing another draw call. Four ranges cover a ground-level view of the
// Marienlyst hub with ~0.1% extra indices over unlimited ranges.
constexpr std::size_t max_terrain_draw_ranges = 4;
// The material overlay is two orders of magnitude smaller than the terrain
// (~200 triangles), so it never deserves more than a couple of draws.
constexpr std::size_t max_material_overlay_draw_ranges = 2;

const std::vector<render_mesh_chunk>& chunks_of(const render_static_mesh* mesh) {
    static const std::vector<render_mesh_chunk> none;
    return mesh == nullptr ? none : mesh->chunks;
}

void draw_index_ranges(const shader_program& shader, const std::vector<render_index_range>& ranges) {
    for (const render_index_range& range : ranges) {
        const std::uintptr_t offset = static_cast<std::uintptr_t>(range.first_index) * sizeof(std::uint32_t);
        draw_elements(shader,
                      GL_TRIANGLES,
                      static_cast<GLsizei>(range.index_count),
                      GL_UNSIGNED_INT,
                      reinterpret_cast<void*>(offset));
    }
}

constexpr int reference_low_res_width = 640;
constexpr int reference_low_res_height = 360;
constexpr int min_low_res_dimension = 120;

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
                            const float alpha,
                            const bool use_vertex_color) {
    shader.set_mat4("u_model", model);
    shader.set_mat4("u_mvp", proj * view * model);
    shader.set_vec3("u_color", color);
    shader.set_float("u_alpha", alpha);
    shader.set_int("u_use_vertex_color", use_vertex_color ? 1 : 0);
}

void set_terrain_draw_state(shader_program& shader,
                            const glm::mat4& model,
                            const glm::mat4& view,
                            const glm::mat4& proj,
                            const glm::vec3& color,
                            const bool use_vertex_color) {
    set_terrain_draw_state(shader, model, view, proj, color, 1.0f, use_vertex_color);
}

struct primitive_geometry {
    unsigned int screen_vao = 0;
    unsigned int cylinder_vao = 0;
    unsigned int ball_vao = 0;
    int cylinder_vertex_count = 0;
    int ball_vertex_count = 0;
};

glm::vec3 local_point_world(const render_data& data, const glm::vec3& local) {
    glm::vec3 forward = data.camera_target - data.camera_position;
    forward.y = 0.0f;
    forward = glm::normalize(glm::length(forward) > 0.0001f ? forward : glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(up, forward));
    return data.camera_position + right * local.x + up * local.y + forward * local.z;
}

glm::mat4 local_model(const render_data& data, const glm::vec3& local, const glm::vec3& rotation, const glm::vec3& scale) {
    glm::vec3 forward = data.camera_target - data.camera_position;
    forward.y = 0.0f;
    forward = glm::normalize(glm::length(forward) > 0.0001f ? forward : glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(up, forward));
    const glm::vec3 position = data.camera_position + right * local.x + up * local.y + forward * local.z;

    glm::mat4 model(1.0f);
    model[0] = glm::vec4(right, 0.0f);
    model[1] = glm::vec4(up, 0.0f);
    model[2] = glm::vec4(forward, 0.0f);
    model[3] = glm::vec4(position, 1.0f);
    model = glm::rotate(model, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::scale(model, scale);
}

void draw_local_panel(shader_program& shader,
                      const glm::mat4& view,
                      const glm::mat4& proj,
                      const render_data& data,
                      const glm::vec3& local,
                      const glm::vec3& rotation,
                      const glm::vec2& half_size,
                      const glm::vec3& color,
                      const float alpha = 1.0f) {
    const glm::mat4 model = local_model(data, local, rotation, glm::vec3(half_size, 1.0f));
    set_terrain_draw_state(shader, model, view, proj, color, alpha, false);
    draw_arrays(shader, GL_TRIANGLES, 0, 6);
}

void draw_local_cylinder(shader_program& shader,
                         const glm::mat4& view,
                         const glm::mat4& proj,
                         const render_data& data,
                         const primitive_geometry& geometry,
                         const glm::vec3& local,
                         const glm::vec3& rotation,
                         const glm::vec3& scale,
                         const glm::vec3& color,
                         const float alpha = 1.0f) {
    glm::mat4 model = local_model(data, local, rotation, glm::vec3(1.0f));
    model = glm::scale(model, scale);
    model = glm::translate(model, glm::vec3(0.0f, -0.5f, 0.0f));
    set_terrain_draw_state(shader, model, view, proj, color, alpha, false);
    glBindVertexArray(geometry.cylinder_vao);
    draw_arrays(shader, GL_TRIANGLES, 0, geometry.cylinder_vertex_count);
}

void draw_local_cylinder_between(shader_program& shader,
                                 const glm::mat4& view,
                                 const glm::mat4& proj,
                                 const render_data& data,
                                 const primitive_geometry& geometry,
                                 const glm::vec3& start_local,
                                 const glm::vec3& end_local,
                                 const float radius,
                                 const glm::vec3& color,
                                 const float alpha = 1.0f) {
    const glm::vec3 start = local_point_world(data, start_local);
    const glm::vec3 end = local_point_world(data, end_local);
    const glm::vec3 axis = end - start;
    const float length = glm::length(axis);
    if (length <= 0.0001f) {
        return;
    }

    const glm::vec3 axis_dir = axis / length;
    const glm::vec3 camera_dir = glm::normalize(data.camera_target - data.camera_position);
    const glm::vec3 reference = std::abs(glm::dot(axis_dir, camera_dir)) > 0.92f
        ? glm::vec3(0.0f, 1.0f, 0.0f)
        : camera_dir;
    const glm::vec3 side = glm::normalize(glm::cross(axis_dir, reference));
    const glm::vec3 bend = glm::normalize(glm::cross(side, axis_dir));

    glm::mat4 model(1.0f);
    model[0] = glm::vec4(side * radius, 0.0f);
    model[1] = glm::vec4(axis, 0.0f);
    model[2] = glm::vec4(bend * radius, 0.0f);
    model[3] = glm::vec4(start, 1.0f);
    set_terrain_draw_state(shader, model, view, proj, color, alpha, false);
    glBindVertexArray(geometry.cylinder_vao);
    draw_arrays(shader, GL_TRIANGLES, 0, geometry.cylinder_vertex_count);
}

void draw_local_sphere(shader_program& shader,
                       const glm::mat4& view,
                       const glm::mat4& proj,
                       const render_data& data,
                       const primitive_geometry& geometry,
                       const glm::vec3& local,
                       const float radius,
                       const glm::vec3& color,
                       const float alpha = 1.0f) {
    const glm::mat4 model = local_model(data, local, glm::vec3(0.0f), glm::vec3(radius));
    set_terrain_draw_state(shader, model, view, proj, color, alpha, false);
    glBindVertexArray(geometry.ball_vao);
    draw_arrays(shader, GL_TRIANGLES, 0, geometry.ball_vertex_count);
}

void draw_cart_model(shader_program& shader,
                     const glm::mat4& view,
                     const glm::mat4& proj,
                     const render_data& data,
                     const primitive_geometry& geometry) {
    if (!data.cart_active) {
        return;
    }

    glBindVertexArray(geometry.screen_vao);
    const glm::vec3 body(0.36f, 0.46f, 0.20f);
    const glm::vec3 trim(0.08f, 0.09f, 0.08f);
    const glm::vec3 cream(0.76f, 0.72f, 0.56f);

    draw_local_panel(shader, view, proj, data, glm::vec3(0.0f, -0.79f, 1.08f), glm::vec3(glm::radians(78.0f), 0.0f, 0.0f), glm::vec2(0.74f, 0.48f), body);
    draw_local_panel(shader, view, proj, data, glm::vec3(0.0f, -0.63f, 0.58f), glm::vec3(glm::radians(82.0f), 0.0f, 0.0f), glm::vec2(0.68f, 0.18f), trim);
    draw_local_panel(shader, view, proj, data, glm::vec3(0.0f, -0.50f, 0.72f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec2(0.64f, 0.13f), cream);
    draw_local_panel(shader, view, proj, data, glm::vec3(-0.78f, -0.63f, 0.86f), glm::vec3(0.0f, glm::radians(90.0f), 0.0f), glm::vec2(0.42f, 0.16f), body);
    draw_local_panel(shader, view, proj, data, glm::vec3(0.78f, -0.63f, 0.86f), glm::vec3(0.0f, glm::radians(90.0f), 0.0f), glm::vec2(0.42f, 0.16f), body);
    draw_local_panel(shader, view, proj, data, glm::vec3(0.0f, 0.23f, 0.72f), glm::vec3(glm::radians(88.0f), 0.0f, 0.0f), glm::vec2(0.84f, 0.42f), glm::vec3(0.72f, 0.68f, 0.47f));
    draw_local_cylinder(shader, view, proj, data, geometry, glm::vec3(0.0f, -0.63f, 0.40f), glm::vec3(glm::radians(68.0f), 0.0f, glm::radians(90.0f)), glm::vec3(0.22f, 0.035f, 0.22f), glm::vec3(0.025f, 0.025f, 0.025f));
    draw_local_cylinder(shader, view, proj, data, geometry, glm::vec3(0.0f, -0.73f, 0.48f), glm::vec3(glm::radians(22.0f), 0.0f, 0.0f), glm::vec3(0.024f, 0.30f, 0.024f), trim);

    const std::array<float, 2> post_x{-0.56f, 0.56f};
    const std::array<float, 2> post_z{0.46f, 1.10f};
    for (const float x : post_x) {
        for (const float z : post_z) {
            draw_local_cylinder(shader, view, proj, data, geometry, glm::vec3(x, -0.17f, z), glm::vec3(0.0f), glm::vec3(0.035f, 0.84f, 0.035f), cream);
        }
    }

    const std::array<float, 2> wheel_x{-0.68f, 0.68f};
    const std::array<float, 2> wheel_z{1.30f};
    for (const float x : wheel_x) {
        for (const float z : wheel_z) {
            draw_local_cylinder(shader, view, proj, data, geometry, glm::vec3(x, -1.08f, z), glm::vec3(0.0f, 0.0f, glm::radians(-90.0f)), glm::vec3(0.23f, 0.16f, 0.23f), glm::vec3(0.025f, 0.025f, 0.025f));
            draw_local_sphere(shader, view, proj, data, geometry, glm::vec3(x, -1.08f, z), 0.085f, glm::vec3(0.58f, 0.56f, 0.48f));
        }
    }
}

void draw_smoke_emote_model(shader_program& shader,
                            const glm::mat4& view,
                            const glm::mat4& proj,
                            const render_data& data,
                            const primitive_geometry& geometry) {
    const float t = std::max(0.0f, data.smoke_emote_elapsed);
    const float ember = 0.55f + 0.45f * std::sin(t * 24.0f);
    const glm::vec3 cigarette_local(0.08f, -0.42f, 0.78f);
    const glm::vec3 cigarette_tip(0.30f, -0.47f, 1.12f);
    draw_local_cylinder_between(shader,
                                view,
                                proj,
                                data,
                                geometry,
                                cigarette_local,
                                cigarette_tip,
                                0.026f,
                                glm::vec3(0.88f, 0.82f, 0.62f));
    draw_local_sphere(shader,
                      view,
                      proj,
                      data,
                      geometry,
                      cigarette_tip,
                      0.046f,
                      glm::vec3(0.95f, 0.20f + ember * 0.20f, 0.10f));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (int i = 0; i < 5; ++i) {
        const float f = static_cast<float>(i);
        const float rise = std::fmod(t * 0.55f + f * 0.18f, 0.90f);
        const float sway = std::sin(t * 4.0f + f * 1.7f) * 0.055f;
        const float alpha = std::clamp(0.48f - rise * 0.42f, 0.04f, 0.42f);
        const float radius = 0.065f + rise * 0.075f;
        draw_local_sphere(shader,
                          view,
                          proj,
                          data,
                          geometry,
                          cigarette_tip + glm::vec3(sway + f * 0.012f, 0.10f + rise, -rise * 0.10f),
                          radius,
                          glm::vec3(0.78f, 0.80f, 0.78f),
                          alpha);
    }
    glDisable(GL_BLEND);
}

void draw_beer_emote_model(shader_program& shader,
                           const glm::mat4& view,
                           const glm::mat4& proj,
                           const render_data& data,
                           const primitive_geometry& geometry) {
    const float bob = std::sin(data.beer_emote_elapsed * 9.0f) * 0.035f;
    const glm::vec3 can_local(-0.24f, -0.50f + bob, 0.82f);
    draw_local_cylinder(shader, view, proj, data, geometry, can_local, glm::vec3(glm::radians(-8.0f), 0.0f, glm::radians(10.0f)), glm::vec3(0.145f, 0.48f, 0.145f), glm::vec3(0.76f, 0.76f, 0.70f));

    glBindVertexArray(geometry.screen_vao);
    draw_local_panel(shader, view, proj, data, can_local + glm::vec3(0.0f, 0.0f, -0.155f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec2(0.118f, 0.108f), glm::vec3(0.78f, 0.18f, 0.12f));
    draw_local_panel(shader, view, proj, data, can_local + glm::vec3(0.0f, 0.25f, -0.03f), glm::vec3(glm::radians(90.0f), 0.0f, 0.0f), glm::vec2(0.052f, 0.024f), glm::vec3(0.18f, 0.18f, 0.16f));
}

void draw_emote_world_model(shader_program& shader,
                            const glm::mat4& view,
                            const glm::mat4& proj,
                            const render_data& data,
                            const primitive_geometry& geometry) {
    if (data.smoke_emote_active) {
        draw_smoke_emote_model(shader, view, proj, data, geometry);
    }
    if (data.beer_emote_active) {
        draw_beer_emote_model(shader, view, proj, data, geometry);
    }
}

void draw_button_outline(overlay_batch& batch,
                         const glm::vec2 center,
                         const glm::vec2 half_size,
                         const glm::vec3 color,
                         const float alpha) {
    constexpr float line_thickness = 0.008f;
    const glm::vec2 top_left(center.x - half_size.x, center.y + half_size.y);
    const glm::vec2 top_right(center.x + half_size.x, center.y + half_size.y);
    const glm::vec2 bottom_left(center.x - half_size.x, center.y - half_size.y);
    const glm::vec2 bottom_right(center.x + half_size.x, center.y - half_size.y);

    draw_overlay_segment(batch, top_left, top_right, line_thickness, color, alpha);
    draw_overlay_segment(batch, top_right, bottom_right, line_thickness, color, alpha);
    draw_overlay_segment(batch, bottom_right, bottom_left, line_thickness, color, alpha);
    draw_overlay_segment(batch, bottom_left, top_left, line_thickness, color, alpha);
}

void draw_control_button_base(overlay_batch& batch,
                              const glm::vec2 center,
                              const glm::vec2 half_size,
                              const bool is_down) {
    const glm::vec3 outline_color(0.66f, 0.68f, 0.66f);
    const glm::vec3 pressed_color(0.88f, 0.70f, 0.30f);

    if (is_down) {
        draw_overlay_quad(batch, center, half_size, pressed_color, 0.90f);
    } else {
        draw_overlay_quad(batch, center, half_size, glm::vec3(0.12f, 0.13f, 0.13f), 0.08f);
    }

    draw_button_outline(batch, center, half_size, outline_color, is_down ? 0.95f : 0.58f);
}

glm::vec3 control_icon_color(const bool is_down) {
    return is_down ? glm::vec3(0.08f, 0.085f, 0.08f) : glm::vec3(0.70f, 0.72f, 0.70f);
}

float control_icon_alpha(const bool is_down) {
    return is_down ? 1.0f : 0.58f;
}

void draw_arrow_icon(overlay_batch& batch,
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

    draw_overlay_segment(batch, tail, tip, thickness, color, alpha);
    draw_overlay_segment(batch, tip, shoulder + side * (radius * 0.34f), thickness, color, alpha);
    draw_overlay_segment(batch, tip, shoulder - side * (radius * 0.34f), thickness, color, alpha);
}

void draw_space_icon(overlay_batch& batch,
                     const glm::vec2 center,
                     const glm::vec2 half_size,
                     const glm::vec3 color,
                     const float alpha) {
    const float thickness = 0.010f;
    const float width = half_size.x * 1.08f;
    const float height = half_size.y * 0.34f;
    const glm::vec2 left(center.x - width * 0.5f, center.y - height * 0.15f);
    const glm::vec2 right(center.x + width * 0.5f, center.y - height * 0.15f);

    draw_overlay_segment(batch, left, right, thickness, color, alpha);
    draw_overlay_segment(batch, left, left + glm::vec2(0.0f, height), thickness, color, alpha);
    draw_overlay_segment(batch, right, right + glm::vec2(0.0f, height), thickness, color, alpha);
}

void draw_space_icon(overlay_batch& batch, const glm::vec2 center, const glm::vec2 half_size, const bool is_down) {
    draw_space_icon(batch, center, half_size, control_icon_color(is_down), control_icon_alpha(is_down));
}

void draw_shift_icon(overlay_batch& batch, const glm::vec2 center, const glm::vec2 half_size, const bool is_down) {
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

    draw_overlay_segment(batch, tip, left_shoulder, thickness, color, alpha);
    draw_overlay_segment(batch, tip, right_shoulder, thickness, color, alpha);
    draw_overlay_segment(batch, left_shoulder, left_base, thickness, color, alpha);
    draw_overlay_segment(batch, right_shoulder, right_base, thickness, color, alpha);
    draw_overlay_segment(batch, left_base, right_base, thickness, color, alpha);
}

void draw_enter_icon(overlay_batch& batch, const glm::vec2 center, const glm::vec2 half_size, const bool is_down) {
    const glm::vec3 color = control_icon_color(is_down);
    const float alpha = control_icon_alpha(is_down);
    const float thickness = 0.010f;
    const glm::vec2 top = center + glm::vec2(half_size.x * 0.44f, half_size.y * 0.40f);
    const glm::vec2 turn = center + glm::vec2(half_size.x * 0.44f, -half_size.y * 0.10f);
    const glm::vec2 tip = center + glm::vec2(-half_size.x * 0.42f, -half_size.y * 0.10f);
    const glm::vec2 shoulder = tip + glm::vec2(half_size.x * 0.30f, 0.0f);

    draw_overlay_segment(batch, top, turn, thickness, color, alpha);
    draw_overlay_segment(batch, turn, tip, thickness, color, alpha);
    draw_overlay_segment(batch, tip, shoulder + glm::vec2(0.0f, half_size.y * 0.22f), thickness, color, alpha);
    draw_overlay_segment(batch, tip, shoulder - glm::vec2(0.0f, half_size.y * 0.22f), thickness, color, alpha);
}

void draw_backspace_icon(overlay_batch& batch, const glm::vec2 center, const glm::vec2 half_size, const bool is_down) {
    const glm::vec3 color = control_icon_color(is_down);
    const float alpha = control_icon_alpha(is_down);
    const float thickness = 0.010f;
    const glm::vec2 tip = center + glm::vec2(-half_size.x * 0.44f, 0.0f);
    const glm::vec2 mid = center + glm::vec2(half_size.x * 0.20f, 0.0f);

    draw_overlay_segment(batch, tip, mid, thickness, color, alpha);
    draw_overlay_segment(batch, tip, center + glm::vec2(-half_size.x * 0.10f, half_size.y * 0.28f), thickness, color, alpha);
    draw_overlay_segment(batch, tip, center + glm::vec2(-half_size.x * 0.10f, -half_size.y * 0.28f), thickness, color, alpha);
    draw_overlay_segment(batch,
                         center + glm::vec2(half_size.x * 0.36f, half_size.y * 0.34f),
                         center + glm::vec2(half_size.x * 0.36f, -half_size.y * 0.34f),
                         thickness,
                         color,
                         alpha);
}

void draw_retee_icon(overlay_batch& batch, const glm::vec2 center, const glm::vec2 half_size, const bool is_down) {
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
        draw_overlay_segment(batch, previous, next, thickness, color, alpha);
        previous = next;
    }

    const glm::vec2 tip = center + glm::vec2(std::cos(end_angle), std::sin(end_angle)) * radius;
    draw_overlay_segment(batch, tip, tip + glm::vec2(half_size.x * 0.18f, half_size.y * 0.08f), thickness, color, alpha);
    draw_overlay_segment(batch, tip, tip + glm::vec2(half_size.x * 0.04f, -half_size.y * 0.22f), thickness, color, alpha);
}

void draw_controls_overlay(overlay_batch& batch, const controls_overlay_state& controls) {
    if (!controls.visible) {
        return;
    }

    const glm::vec2 dpad_half(0.060f, 0.060f);
    const glm::vec2 small_half(0.066f, 0.052f);
    const glm::vec2 wide_half(0.138f, 0.052f);
    const glm::vec2 dpad_center(0.79f, 0.02f);

    draw_control_button_base(batch, dpad_center + glm::vec2(0.0f, 0.126f), dpad_half, controls.up_down);
    draw_arrow_icon(batch, dpad_center + glm::vec2(0.0f, 0.126f), glm::vec2(0.0f, 1.0f), dpad_half, controls.up_down);

    draw_control_button_base(batch, dpad_center + glm::vec2(-0.070f, 0.0f), dpad_half, controls.left_down);
    draw_arrow_icon(batch, dpad_center + glm::vec2(-0.070f, 0.0f), glm::vec2(-1.0f, 0.0f), dpad_half, controls.left_down);

    draw_control_button_base(batch, dpad_center + glm::vec2(0.070f, 0.0f), dpad_half, controls.right_down);
    draw_arrow_icon(batch, dpad_center + glm::vec2(0.070f, 0.0f), glm::vec2(1.0f, 0.0f), dpad_half, controls.right_down);

    draw_control_button_base(batch, dpad_center + glm::vec2(0.0f, -0.126f), dpad_half, controls.down_down);
    draw_arrow_icon(batch, dpad_center + glm::vec2(0.0f, -0.126f), glm::vec2(0.0f, -1.0f), dpad_half, controls.down_down);

    draw_control_button_base(batch, glm::vec2(0.79f, -0.32f), wide_half, controls.space_down);
    draw_space_icon(batch, glm::vec2(0.79f, -0.32f), wide_half, controls.space_down);

    draw_control_button_base(batch, glm::vec2(0.70f, -0.46f), small_half, controls.shift_down);
    draw_shift_icon(batch, glm::vec2(0.70f, -0.46f), small_half, controls.shift_down);

    draw_control_button_base(batch, glm::vec2(0.88f, -0.46f), small_half, controls.enter_down);
    draw_enter_icon(batch, glm::vec2(0.88f, -0.46f), small_half, controls.enter_down);

    draw_control_button_base(batch, glm::vec2(0.70f, -0.60f), small_half, controls.backspace_down);
    draw_backspace_icon(batch, glm::vec2(0.70f, -0.60f), small_half, controls.backspace_down);

    draw_control_button_base(batch, glm::vec2(0.88f, -0.60f), small_half, controls.retee_down);
    draw_retee_icon(batch, glm::vec2(0.88f, -0.60f), small_half, controls.retee_down);

    const glm::vec2 key_half(0.052f, 0.046f);
    draw_control_button_base(batch, glm::vec2(0.70f, -0.74f), key_half, controls.key_1_down);
    draw_pixel_text_centered(batch,
                             "1",
                             glm::vec2(0.70f, -0.74f),
                             fit_pixel_size("1", key_half, 0.020f, 0.010f, 0.88f),
                             control_icon_color(controls.key_1_down));

    draw_control_button_base(batch, glm::vec2(0.88f, -0.74f), key_half, controls.key_2_down);
    draw_pixel_text_centered(batch,
                             "2",
                             glm::vec2(0.88f, -0.74f),
                             fit_pixel_size("2", key_half, 0.020f, 0.010f, 0.88f),
                             control_icon_color(controls.key_2_down));
}

enum class help_control_icon {
    arrows,
    space,
    shift,
    enter,
    backspace,
    retee,
    key_1,
    key_2
};

void draw_help_text_lines(overlay_batch& batch,
                          const glm::vec2 top_left,
                          const std::array<const char*, 4>& lines,
                          const float pixel_size,
                          const glm::vec3 color) {
    constexpr float line_gap = 9.0f;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i] == nullptr || lines[i][0] == '\0') {
            continue;
        }
        draw_pixel_text_left(batch,
                             lines[i],
                             top_left - glm::vec2(0.0f, static_cast<float>(i) * pixel_size * line_gap),
                             pixel_size,
                             color);
    }
}

void draw_help_control_icon(overlay_batch& batch,
                            const help_control_icon icon,
                            const glm::vec2 center) {
    const bool is_down = false;
    const glm::vec2 small_half(0.066f, 0.052f);
    const glm::vec2 wide_half(0.138f, 0.052f);
    const glm::vec2 key_half(0.052f, 0.046f);

    switch (icon) {
    case help_control_icon::arrows: {
        const glm::vec2 dpad_half(0.043f, 0.043f);
        const float offset = 0.052f;
        draw_control_button_base(batch, center + glm::vec2(0.0f, offset), dpad_half, is_down);
        draw_arrow_icon(batch, center + glm::vec2(0.0f, offset), glm::vec2(0.0f, 1.0f), dpad_half, is_down);
        draw_control_button_base(batch, center + glm::vec2(-offset, 0.0f), dpad_half, is_down);
        draw_arrow_icon(batch, center + glm::vec2(-offset, 0.0f), glm::vec2(-1.0f, 0.0f), dpad_half, is_down);
        draw_control_button_base(batch, center + glm::vec2(offset, 0.0f), dpad_half, is_down);
        draw_arrow_icon(batch, center + glm::vec2(offset, 0.0f), glm::vec2(1.0f, 0.0f), dpad_half, is_down);
        draw_control_button_base(batch, center + glm::vec2(0.0f, -offset), dpad_half, is_down);
        draw_arrow_icon(batch, center + glm::vec2(0.0f, -offset), glm::vec2(0.0f, -1.0f), dpad_half, is_down);
        break;
    }
    case help_control_icon::space:
        draw_control_button_base(batch, center, wide_half, is_down);
        draw_space_icon(batch, center, wide_half, is_down);
        break;
    case help_control_icon::shift:
        draw_control_button_base(batch, center, small_half, is_down);
        draw_shift_icon(batch, center, small_half, is_down);
        break;
    case help_control_icon::enter:
        draw_control_button_base(batch, center, small_half, is_down);
        draw_enter_icon(batch, center, small_half, is_down);
        break;
    case help_control_icon::backspace:
        draw_control_button_base(batch, center, small_half, is_down);
        draw_backspace_icon(batch, center, small_half, is_down);
        break;
    case help_control_icon::retee:
        draw_control_button_base(batch, center, small_half, is_down);
        draw_retee_icon(batch, center, small_half, is_down);
        break;
    case help_control_icon::key_1:
        draw_control_button_base(batch, center, key_half, is_down);
        draw_pixel_text_centered(batch, "1", center, 0.020f, control_icon_color(is_down));
        break;
    case help_control_icon::key_2:
        draw_control_button_base(batch, center, key_half, is_down);
        draw_pixel_text_centered(batch, "2", center, 0.020f, control_icon_color(is_down));
        break;
    }
}

void draw_help_control_row(overlay_batch& batch,
                           const help_control_icon icon,
                           const glm::vec2 icon_center,
                           const glm::vec2 label_top_left,
                           const std::array<const char*, 4>& lines) {
    draw_help_control_icon(batch, icon, icon_center);
    draw_help_text_lines(batch, label_top_left, lines, 0.0092f, glm::vec3(0.84f, 0.84f, 0.74f));
}

void draw_startup_help_screen(overlay_batch& batch) {
    const glm::vec2 left_icon(-0.66f, 0.0f);
    const glm::vec2 left_label(-0.46f, 0.0f);
    const glm::vec2 right_icon(0.30f, 0.0f);
    const glm::vec2 right_label(0.45f, 0.0f);

    draw_help_control_row(batch,
                          help_control_icon::arrows,
                          left_icon + glm::vec2(0.0f, 0.42f),
                          left_label + glm::vec2(0.0f, 0.51f),
                          {{"ARROWS", "WALK / AIM", "CLUB UP / DOWN", ""}});
    draw_help_control_row(batch,
                          help_control_icon::space,
                          left_icon + glm::vec2(0.0f, 0.15f),
                          left_label + glm::vec2(0.0f, 0.22f),
                          {{"INTERACT", "START SWING", "SET POWER", "DRIFT CART"}});
    draw_help_control_row(batch,
                          help_control_icon::shift,
                          left_icon + glm::vec2(0.0f, -0.22f),
                          left_label + glm::vec2(0.0f, -0.18f),
                          {{"LEFT SHIFT", "HOLD CART", "", ""}});
    draw_help_control_row(batch,
                          help_control_icon::shift,
                          left_icon + glm::vec2(0.0f, -0.43f),
                          left_label + glm::vec2(0.0f, -0.39f),
                          {{"SHIFT", "RANGEFINDER", "", ""}});

    draw_help_control_row(batch,
                          help_control_icon::enter,
                          right_icon + glm::vec2(0.0f, 0.43f),
                          right_label + glm::vec2(0.0f, 0.50f),
                          {{"ENTER", "COURSE MAP", "MENU SELECT", ""}});
    draw_help_control_row(batch,
                          help_control_icon::backspace,
                          right_icon + glm::vec2(0.0f, 0.22f),
                          right_label + glm::vec2(0.0f, 0.25f),
                          {{"BACKSPACE /", "ESCAPE", "CANCEL /", "BACK"}});
    draw_help_control_row(batch,
                          help_control_icon::retee,
                          right_icon + glm::vec2(0.0f, -0.12f),
                          right_label + glm::vec2(0.0f, -0.12f),
                          {{"R", "RETEE", "", ""}});
    draw_help_control_row(batch,
                          help_control_icon::key_1,
                          right_icon + glm::vec2(0.0f, -0.32f),
                          right_label + glm::vec2(0.0f, -0.30f),
                          {{"1", "SMOKE", "", ""}});
    draw_help_control_row(batch,
                          help_control_icon::key_2,
                          right_icon + glm::vec2(0.0f, -0.52f),
                          right_label + glm::vec2(0.0f, -0.50f),
                          {{"2", "BEER", "", ""}});
}

void draw_fps_counter(overlay_batch& batch, const std::string& label) {
    if (label.empty()) {
        return;
    }

    draw_pixel_text_left(batch, label, glm::vec2(-0.96f, 0.92f), 0.009f, glm::vec3(0.96f, 0.78f, 0.18f));
}

void draw_profile_overlay(overlay_batch& batch, const frame_profile& profile) {
    const std::vector<std::string> lines = format_profile_overlay_lines(profile);
    if (lines.empty()) {
        return;
    }

    constexpr float pixel_size = 0.006f;
    constexpr float line_step = 0.052f;
    const glm::vec3 color(0.62f, 0.90f, 0.72f);
    glm::vec2 cursor(-0.96f, 0.855f);
    for (const std::string& line : lines) {
        draw_pixel_text_left(batch, line, cursor, pixel_size, color);
        cursor.y -= line_step;
    }
}

void draw_club_label(overlay_batch& batch, const std::string& label) {
    const glm::vec3 label_color(0.90f, 0.88f, 0.76f);
    const glm::vec3 panel_color(0.055f, 0.06f, 0.07f);
    const glm::vec2 panel_center(0.78f, 0.78f);
    const glm::vec2 panel_half(0.17f, 0.12f);
    draw_overlay_quad(batch, panel_center, panel_half, panel_color);

    const float pixel_size = fit_pixel_size(label, panel_half, 0.022f, 0.010f);
    draw_pixel_text_centered(batch, label, panel_center, pixel_size, label_color);
}

void draw_interact_prompt(overlay_batch& batch) {
    const glm::vec3 prompt_color(0.95f, 0.82f, 0.28f);
    draw_space_icon(batch, glm::vec2(0.0f, -0.56f), glm::vec2(0.16f, 0.075f), prompt_color, 1.0f);
}

void draw_power_meter(overlay_batch& batch, const float swing_power) {
    const float power = std::clamp(swing_power, 0.0f, 1.0f);
    const glm::vec3 panel_color(0.055f, 0.060f, 0.065f);
    const glm::vec3 outline_color(0.62f, 0.64f, 0.60f);
    const glm::vec3 text_color(0.88f, 0.86f, 0.72f);
    const glm::vec3 amber(0.92f, 0.70f, 0.18f);

    const glm::vec2 panel_center(-0.62f, -0.72f);
    const glm::vec2 panel_half(0.32f, 0.155f);
    draw_overlay_quad(batch, panel_center + glm::vec2(0.012f, -0.014f), panel_half, glm::vec3(0.0f), 0.22f);
    draw_overlay_quad(batch, panel_center, panel_half, panel_color, 0.78f);
    draw_button_outline(batch, panel_center, panel_half, outline_color, 0.54f);

    draw_pixel_text_left(batch, "POWER", panel_center + glm::vec2(-0.275f, 0.105f), 0.015f, text_color);

    const glm::vec2 track_center = panel_center + glm::vec2(0.020f, -0.012f);
    const glm::vec2 track_half(0.245f, 0.035f);
    const float track_left = track_center.x - track_half.x;
    const float track_right = track_center.x + track_half.x;
    const float track_width = track_half.x * 2.0f;

    draw_overlay_quad(batch, track_center, track_half, glm::vec3(0.030f, 0.032f, 0.034f), 0.92f);
    draw_button_outline(batch, track_center, track_half, outline_color, 0.56f);

    if (power > 0.0f) {
        const float fill_half_width = track_half.x * power;
        const glm::vec2 fill_center(track_left + fill_half_width, track_center.y);
        draw_overlay_quad(batch, fill_center, glm::vec2(fill_half_width, track_half.y * 0.58f), amber, 0.92f);
    }

    const float sweet_x = track_left + track_width * 0.86f;
    draw_overlay_segment(batch,
                         glm::vec2(sweet_x, track_center.y - track_half.y * 1.08f),
                         glm::vec2(sweet_x, track_center.y + track_half.y * 1.08f),
                         0.006f,
                         glm::vec3(0.96f, 0.88f, 0.55f),
                         0.88f);

    const float needle_x = track_left + track_width * power;
    draw_overlay_segment(batch,
                         glm::vec2(needle_x, track_center.y - 0.055f),
                         glm::vec2(needle_x, track_center.y + 0.055f),
                         0.008f,
                         glm::vec3(0.98f, 0.80f, 0.24f),
                         0.96f);

    const std::array<float, 3> ticks{0.0f, 0.5f, 1.0f};
    for (const float tick : ticks) {
        const float x = track_left + track_width * tick;
        draw_overlay_segment(batch,
                             glm::vec2(x, track_center.y - 0.058f),
                             glm::vec2(x, track_center.y - 0.080f),
                             0.005f,
                             outline_color,
                             0.72f);
    }

    draw_pixel_text_centered(batch, "0", glm::vec2(track_left, panel_center.y - 0.112f), 0.011f, text_color);
    draw_pixel_text_centered(batch, "50", glm::vec2(track_center.x, panel_center.y - 0.112f), 0.011f, text_color);
    draw_pixel_text_centered(batch, "100", glm::vec2(track_right, panel_center.y - 0.112f), 0.011f, text_color);
}

void draw_cart_hud(overlay_batch& batch, const render_data& data) {
    if (!data.cart_active) {
        return;
    }

    const glm::vec3 panel_color(0.055f, 0.060f, 0.065f);
    const glm::vec3 outline_color(0.60f, 0.62f, 0.56f);
    const glm::vec3 text_color(0.94f, 0.86f, 0.52f);
    const glm::vec3 meter_color = data.cart_drifting ? glm::vec3(0.96f, 0.56f, 0.22f) : glm::vec3(0.72f, 0.80f, 0.36f);

    const glm::vec2 panel_center(-0.74f, 0.72f);
    const glm::vec2 panel_half(0.19f, 0.13f);
    draw_overlay_quad(batch, panel_center + glm::vec2(0.012f, -0.012f), panel_half, glm::vec3(0.0f), 0.18f);
    draw_overlay_quad(batch, panel_center, panel_half, panel_color, 0.82f);
    draw_button_outline(batch, panel_center, panel_half, outline_color, 0.52f);

    draw_pixel_text_left(batch, "CART", panel_center + glm::vec2(-0.150f, 0.082f), 0.015f, text_color);
    draw_pixel_text_left(batch,
                         data.cart_drifting ? "DRIFT" : "DRIVE",
                         panel_center + glm::vec2(-0.150f, 0.020f),
                         0.013f,
                         meter_color);

    const glm::vec2 track_center = panel_center + glm::vec2(0.030f, -0.046f);
    const glm::vec2 track_half(0.120f, 0.024f);
    draw_overlay_quad(batch, track_center, track_half, glm::vec3(0.028f, 0.030f, 0.032f), 0.92f);
    draw_button_outline(batch, track_center, track_half, outline_color, 0.46f);

    const float speed_ratio = std::clamp(data.cart_speed / 18.0f, 0.0f, 1.0f);
    if (speed_ratio > 0.0f) {
        const float fill_half_width = track_half.x * speed_ratio;
        draw_overlay_quad(batch,
                          glm::vec2(track_center.x - track_half.x + fill_half_width, track_center.y),
                          glm::vec2(fill_half_width, track_half.y * 0.60f),
                          meter_color,
                          0.94f);
    }
}

void draw_skills_panel(overlay_batch& batch, const std::vector<render_skill_progress>& skills) {
    if (skills.empty()) {
        return;
    }

    const glm::vec2 center(0.0f, 0.16f);
    const glm::vec2 half(0.58f, 0.44f);
    const glm::vec3 panel_color(0.050f, 0.055f, 0.060f);
    const glm::vec3 outline_color(0.62f, 0.64f, 0.60f);
    const glm::vec3 title_color(0.95f, 0.78f, 0.28f);
    const glm::vec3 text_color(0.88f, 0.86f, 0.72f);
    const glm::vec3 muted_color(0.58f, 0.60f, 0.56f);

    draw_overlay_quad(batch, center + glm::vec2(0.018f, -0.020f), half, glm::vec3(0.0f), 0.32f);
    draw_overlay_quad(batch, center, half, panel_color, 0.88f);
    draw_button_outline(batch, center, half, outline_color, 0.64f);

    draw_pixel_text_centered(batch, "SKILLS", center + glm::vec2(0.0f, half.y - 0.070f), 0.020f, title_color);
    draw_pixel_text_left(batch, "NAME", center + glm::vec2(-0.48f, half.y - 0.145f), 0.010f, muted_color);
    draw_pixel_text_left(batch, "LV", center + glm::vec2(0.02f, half.y - 0.145f), 0.010f, muted_color);
    draw_pixel_text_left(batch, "XP", center + glm::vec2(0.16f, half.y - 0.145f), 0.010f, muted_color);
    draw_pixel_text_left(batch, "NEXT", center + glm::vec2(0.36f, half.y - 0.145f), 0.010f, muted_color);

    constexpr float row_gap = 0.105f;
    for (std::size_t i = 0; i < skills.size(); ++i) {
        const render_skill_progress& skill = skills[i];
        const float y = center.y + half.y - 0.225f - static_cast<float>(i) * row_gap;
        const glm::vec3 row_color = i % 2 == 0 ? glm::vec3(0.075f, 0.080f, 0.078f) : glm::vec3(0.060f, 0.064f, 0.066f);
        draw_overlay_quad(batch, glm::vec2(center.x, y - 0.016f), glm::vec2(0.50f, 0.038f), row_color, 0.74f);
        draw_pixel_text_left(batch, skill.label, glm::vec2(center.x - 0.48f, y), 0.012f, text_color);
        draw_pixel_text_left(batch, std::to_string(std::max(1, skill.level)), glm::vec2(center.x + 0.02f, y), 0.012f, text_color);
        draw_pixel_text_left(batch, std::to_string(std::max(0, skill.xp)), glm::vec2(center.x + 0.16f, y), 0.012f, text_color);
        draw_pixel_text_left(batch, std::to_string(std::max(0, skill.xp_to_next)), glm::vec2(center.x + 0.36f, y), 0.012f, text_color);
    }

    draw_pixel_text_centered(batch, "CAPS LOCK", center + glm::vec2(0.0f, -half.y + 0.055f), 0.009f, muted_color);
}

void draw_xp_icon_cell(overlay_batch& batch,
                       const glm::vec2 icon_center,
                       const int x,
                       const int y,
                       const float cell_size,
                       const glm::vec3 color,
                       const float alpha) {
    draw_overlay_quad(batch,
                      icon_center + glm::vec2((static_cast<float>(x) - 2.0f) * cell_size,
                                              (2.0f - static_cast<float>(y)) * cell_size),
                      glm::vec2(cell_size * 0.43f),
                      color,
                      alpha);
}

void draw_xp_icon_pattern(overlay_batch& batch,
                          const glm::vec2 icon_center,
                          const std::array<const char*, 5>& pattern,
                          const float cell_size,
                          const glm::vec3 color,
                          const float alpha) {
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            if (pattern[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] == '#') {
                draw_xp_icon_cell(batch, icon_center, x, y, cell_size, color, alpha);
            }
        }
    }
}

void draw_xp_icon(overlay_batch& batch,
                  const skill_icon_id icon,
                  const glm::vec2 center,
                  const float alpha) {
    const float cell = 0.010f;
    switch (icon) {
    case skill_icon_id::golf_swing:
        draw_xp_icon_pattern(batch,
                             center,
                             std::array<const char*, 5>{{"..#..", "..#..", ".##..", "...#.", "...##"}},
                             cell,
                             glm::vec3(0.92f, 0.78f, 0.38f),
                             alpha);
        draw_overlay_quad(batch, center + glm::vec2(-0.016f, -0.020f), glm::vec2(0.008f), glm::vec3(0.92f), alpha);
        break;
    case skill_icon_id::smoking:
        draw_xp_icon_pattern(batch,
                             center,
                             std::array<const char*, 5>{{".....", ".###.", "...#.", "####.", "....."}},
                             cell,
                             glm::vec3(0.90f, 0.84f, 0.66f),
                             alpha);
        draw_xp_icon_cell(batch, center, 4, 2, cell, glm::vec3(0.86f, 0.38f, 0.22f), alpha);
        break;
    case skill_icon_id::fitness:
        draw_xp_icon_pattern(batch,
                             center,
                             std::array<const char*, 5>{{".....", ".##..", ".###.", "..###", "....."}},
                             cell,
                             glm::vec3(0.52f, 0.82f, 0.58f),
                             alpha);
        break;
    case skill_icon_id::generic:
    default:
        draw_xp_icon_pattern(batch,
                             center,
                             std::array<const char*, 5>{{"..#..", ".###.", "#####", ".###.", "..#.."}},
                             cell,
                             glm::vec3(0.70f, 0.78f, 0.92f),
                             alpha);
        break;
    }
}

void draw_xp_drops(overlay_batch& batch, const std::vector<render_xp_drop>& drops) {
    if (drops.empty()) {
        return;
    }

    const glm::vec3 panel_color(0.035f, 0.040f, 0.040f);
    const glm::vec3 outline_color(0.68f, 0.70f, 0.56f);
    const glm::vec3 text_color(0.96f, 0.86f, 0.38f);
    const glm::vec2 row_half(0.155f, 0.043f);
    const float row_gap = 0.092f;
    const float right = 0.585f;
    const float start_y = 0.565f;
    const float travel_y = 0.62f;

    for (std::size_t i = 0; i < drops.size(); ++i) {
        const render_xp_drop& drop = drops[i];
        if (drop.xp <= 0) {
            continue;
        }

        const float progress = drop.lifetime <= 0.0f ? 1.0f : std::clamp(drop.age / drop.lifetime, 0.0f, 1.0f);
        const float alpha = progress > 0.78f ? std::max(0.16f, 1.0f - (progress - 0.78f) / 0.22f) : 1.0f;
        const glm::vec2 center(right - row_half.x, start_y - static_cast<float>(i) * row_gap + progress * travel_y);

        draw_overlay_quad(batch, center + glm::vec2(0.010f, -0.010f), row_half, glm::vec3(0.0f), 0.18f * alpha);
        draw_overlay_quad(batch, center, row_half, panel_color, 0.72f * alpha);
        draw_button_outline(batch, center, row_half, outline_color, 0.42f * alpha);
        draw_xp_icon(batch, drop.icon, center + glm::vec2(-0.105f, 0.0f), alpha);

        const std::string label = "+" + std::to_string(std::max(0, drop.xp)) + " XP";
        const float pixel_size = fit_pixel_size(label, glm::vec2(0.100f, 0.030f), 0.013f, 0.008f);
        draw_pixel_text_left(batch, label, center + glm::vec2(-0.055f, 0.021f), pixel_size, text_color * alpha);
    }
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

void draw_hole_thumbnail(overlay_batch& batch,
                         const render_hole_preview& preview,
                         const glm::vec2 center,
                         const glm::vec2 half_size) {
    draw_overlay_quad(batch, center, half_size, glm::vec3(0.028f, 0.034f, 0.030f), 0.96f);
    draw_button_outline(batch, center, half_size, glm::vec3(0.42f, 0.44f, 0.40f), 0.42f);

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

    if (preview.control_points.size() >= 2) {
        const float fairway_width = std::max(0.010f, preview.fairway_width * scale * 0.35f);
        for (std::size_t i = 1; i < preview.control_points.size(); ++i) {
            const glm::vec2 a = preview_point(preview.control_points[i - 1], center, inset_half, padded_min, scale, rotate_long_axis);
            const glm::vec2 b = preview_point(preview.control_points[i], center, inset_half, padded_min, scale, rotate_long_axis);
            draw_overlay_segment(batch, a, b, fairway_width, glm::vec3(0.18f, 0.46f, 0.18f), 0.82f);
            draw_overlay_segment(batch, a, b, 0.006f, glm::vec3(0.62f, 0.78f, 0.38f), 0.55f);
        }
    }

    for (const material_zone& zone : preview.material_zones) {
        const glm::vec3 color = thumbnail_zone_color(zone.type);
        if (zone.has_bounds) {
            const glm::vec2 a = preview_point(zone.bounds_min, center, inset_half, padded_min, scale, rotate_long_axis);
            const glm::vec2 b = preview_point(zone.bounds_max, center, inset_half, padded_min, scale, rotate_long_axis);
            draw_overlay_quad(batch, (a + b) * 0.5f, glm::abs(b - a) * 0.5f, color, 0.64f);
        } else if (zone.has_radius) {
            const glm::vec2 p = preview_point(zone.center, center, inset_half, padded_min, scale, rotate_long_axis);
            const float radius = std::max(0.010f, zone.radius * scale);
            draw_overlay_quad(batch, p, glm::vec2(radius), color, 0.64f);
        }
    }

    const glm::vec2 tee = preview_point(preview.tee_position, center, inset_half, padded_min, scale, rotate_long_axis);
    const glm::vec2 pin = preview_point(preview.pin_position, center, inset_half, padded_min, scale, rotate_long_axis);
    draw_overlay_quad(batch, tee, glm::vec2(0.014f), glm::vec3(0.88f, 0.80f, 0.48f), 0.94f);
    draw_overlay_quad(batch, pin, glm::vec2(0.012f, 0.028f), glm::vec3(0.88f, 0.18f, 0.12f), 0.94f);
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

void draw_startup_menu(overlay_batch& batch, const render_startup_menu& menu) {
    if (menu.screen == startup_menu_screen::none) {
        return;
    }

    draw_overlay_quad(batch, glm::vec2(0.0f), glm::vec2(1.0f), glm::vec3(0.0f, 0.0f, 0.0f), 0.72f);
    draw_overlay_quad(batch, glm::vec2(0.0f, 0.0f), glm::vec2(0.86f, 0.88f), glm::vec3(0.012f, 0.014f, 0.014f), 0.30f);
    const float menu_title_pixel = fit_pixel_size(menu.title, glm::vec2(0.78f, 0.10f), 0.028f, 0.014f);
    draw_pixel_text_centered(batch, menu.title, glm::vec2(0.0f, 0.80f), menu_title_pixel, glm::vec3(0.95f, 0.78f, 0.28f));
    if (!menu.subtitle.empty()) {
        const float menu_subtitle_pixel = fit_pixel_size(menu.subtitle, glm::vec2(0.70f, 0.065f), 0.015f, 0.010f);
        draw_pixel_text_centered(batch, menu.subtitle, glm::vec2(0.0f, 0.68f), menu_subtitle_pixel, glm::vec3(0.72f, 0.72f, 0.62f));
    }

    if (menu.screen == startup_menu_screen::help) {
        draw_startup_help_screen(batch);
    }

    const glm::vec2 tile_half = startup_tile_half_size(menu.screen);
    for (std::size_t i = 0; i < menu.tiles.size(); ++i) {
        const render_startup_tile& tile = menu.tiles[i];
        const glm::vec2 center = startup_tile_center(menu.screen, static_cast<int>(i));
        const glm::vec3 panel_color = tile.selected ? glm::vec3(0.20f, 0.16f, 0.065f) : glm::vec3(0.070f, 0.075f, 0.075f);
        const glm::vec3 outline = tile.selected ? glm::vec3(0.94f, 0.72f, 0.22f) : glm::vec3(0.50f, 0.52f, 0.48f);
        draw_overlay_quad(batch, center, tile_half, panel_color, tile.selected ? 0.94f : 0.76f);
        draw_button_outline(batch, center, tile_half, outline, tile.selected ? 0.94f : 0.44f);

        if (tile.has_preview) {
            draw_hole_thumbnail(batch, tile.preview, center + glm::vec2(0.0f, 0.030f), glm::vec2(tile_half.x * 0.86f, tile_half.y * 0.48f));

            const glm::vec2 title_half(tile_half.x * 0.86f, tile_half.y * 0.22f);
            const glm::vec2 subtitle_half(tile_half.x * 0.86f, tile_half.y * 0.18f);
            const float title_pixel = fit_pixel_size(tile.title, title_half, 0.012f, 0.008f);
            const float subtitle_pixel = fit_pixel_size(tile.subtitle, subtitle_half, 0.010f, 0.007f);

            draw_pixel_text_left(batch,
                                 tile.title,
                                 center + glm::vec2(-tile_half.x * 0.86f, -tile_half.y * 0.28f),
                                 title_pixel,
                                 glm::vec3(0.90f, 0.88f, 0.76f));
            draw_pixel_text_left(batch,
                                 tile.subtitle,
                                 center + glm::vec2(-tile_half.x * 0.86f, -tile_half.y * 0.58f),
                                 subtitle_pixel,
                                 glm::vec3(0.68f, 0.69f, 0.62f));
        } else {
            const glm::vec2 title_half(tile_half.x * 0.82f, tile_half.y * 0.32f);
            const glm::vec2 subtitle_half(tile_half.x * 0.82f, tile_half.y * 0.26f);
            const float title_pixel = fit_pixel_size(tile.title, title_half, 0.020f, 0.010f);
            const float subtitle_pixel = fit_pixel_size(tile.subtitle, subtitle_half, 0.012f, 0.008f);

            draw_pixel_text_centered(batch,
                                     tile.title,
                                     center + glm::vec2(0.0f, 0.018f),
                                     title_pixel,
                                     glm::vec3(0.90f, 0.88f, 0.76f));
            draw_pixel_text_centered(batch,
                                     tile.subtitle,
                                     center + glm::vec2(0.0f, -0.050f),
                                     subtitle_pixel,
                                     glm::vec3(0.66f, 0.67f, 0.61f));
        }
    }

    if (!menu.footer.empty()) {
        const float footer_pixel = fit_pixel_size(menu.footer, glm::vec2(0.78f, 0.05f), 0.013f, 0.009f);
        draw_pixel_text_centered(batch, menu.footer, glm::vec2(0.0f, -0.86f), footer_pixel, glm::vec3(0.56f, 0.57f, 0.52f));
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

void draw_rangefinder_view(overlay_batch& batch,
                           const glm::mat4& view,
                           const glm::mat4& proj,
                           const render_data& data) {
    draw_overlay_quad(batch, glm::vec2(0.0f), glm::vec2(1.0f), glm::vec3(0.16f, 0.34f, 0.24f), 0.18f);
    draw_overlay_quad(batch, glm::vec2(-0.91f, 0.0f), glm::vec2(0.18f, 1.0f), glm::vec3(0.01f, 0.018f, 0.014f), 0.52f);
    draw_overlay_quad(batch, glm::vec2(0.91f, 0.0f), glm::vec2(0.18f, 1.0f), glm::vec3(0.01f, 0.018f, 0.014f), 0.52f);
    draw_overlay_quad(batch, glm::vec2(0.0f, 0.89f), glm::vec2(1.0f, 0.22f), glm::vec3(0.01f, 0.018f, 0.014f), 0.45f);
    draw_overlay_quad(batch, glm::vec2(0.0f, -0.89f), glm::vec2(1.0f, 0.22f), glm::vec3(0.01f, 0.018f, 0.014f), 0.45f);

    const glm::vec3 reticle_color(0.62f, 0.96f, 0.64f);
    draw_overlay_quad(batch, glm::vec2(0.0f, 0.0f), glm::vec2(0.006f, 0.11f), reticle_color, 0.72f);
    draw_overlay_quad(batch, glm::vec2(0.0f, 0.0f), glm::vec2(0.11f, 0.006f), reticle_color, 0.72f);
    draw_overlay_quad(batch, glm::vec2(-0.18f, 0.0f), glm::vec2(0.055f, 0.006f), reticle_color, 0.56f);
    draw_overlay_quad(batch, glm::vec2(0.18f, 0.0f), glm::vec2(0.055f, 0.006f), reticle_color, 0.56f);
    draw_overlay_quad(batch, glm::vec2(0.0f, -0.18f), glm::vec2(0.006f, 0.055f), reticle_color, 0.56f);
    draw_overlay_quad(batch, glm::vec2(0.0f, 0.18f), glm::vec2(0.006f, 0.055f), reticle_color, 0.56f);

    glm::vec2 pin_screen(0.0f);
    const glm::vec3 label_anchor = data.pin_position + glm::vec3(0.0f, data.pin_visual_height_meters + 0.36f, 0.0f);
    if (!project_to_screen(view, proj, label_anchor, pin_screen)) {
        return;
    }

    pin_screen.x = std::max(-0.78f, std::min(0.78f, pin_screen.x));
    pin_screen.y = std::max(-0.68f, std::min(0.82f, pin_screen.y));
    const glm::vec2 label_max_half(0.22f, 0.065f);
    const float pixel_size = fit_pixel_size(data.rangefinder_distance_label, label_max_half, 0.020f, 0.010f);
    const float label_width = pixel_text_width(data.rangefinder_distance_label, pixel_size);
    const glm::vec2 panel_half(std::max(0.12f, label_width * 0.5f + 0.035f), 0.070f);
    draw_overlay_quad(batch, pin_screen + glm::vec2(0.0f, 0.015f), panel_half, glm::vec3(0.015f, 0.032f, 0.024f), 0.78f);
    draw_pixel_text_centered(batch,
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

    if (data.terrain_mesh != nullptr && data.terrain_mesh->bounds.valid) {
        // Per-axis min/max of the cached AABB equals expanding by every vertex.
        expand_map_bounds(data.terrain_mesh->bounds.min, min_point, max_point);
        expand_map_bounds(data.terrain_mesh->bounds.max, min_point, max_point);
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

void draw_map_marker(overlay_batch& batch,
                     const glm::vec2 position,
                     const glm::vec3 color,
                     const float radius) {
    draw_overlay_quad(batch, position, glm::vec2(radius), glm::vec3(0.04f, 0.035f, 0.025f), 0.55f);
    draw_overlay_quad(batch, position, glm::vec2(radius * 0.64f), color, 1.0f);
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

void draw_filled_map_triangle(overlay_batch& batch,
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

        draw_overlay_quad(batch,
                          glm::vec2((x0 + x1) * 0.5f, y),
                          glm::vec2((x1 - x0) * 0.5f, strip_height * 0.56f),
                          color,
                          0.82f);
    }
}

void draw_filled_map_terrain(overlay_batch& batch, const course_map_layout& layout, const render_data& data) {
    const render_static_mesh* terrain_mesh = data.terrain_mesh;
    if (terrain_mesh == nullptr || terrain_mesh->vertices.empty() || terrain_mesh->indices.size() < 3) {
        return;
    }

    for (std::size_t i = 0; i + 2 < terrain_mesh->indices.size(); i += 3) {
        const std::uint32_t ia = terrain_mesh->indices[i];
        const std::uint32_t ib = terrain_mesh->indices[i + 1];
        const std::uint32_t ic = terrain_mesh->indices[i + 2];
        if (ia >= terrain_mesh->vertices.size() ||
            ib >= terrain_mesh->vertices.size() ||
            ic >= terrain_mesh->vertices.size()) {
            continue;
        }

        const render_terrain_vertex& va = terrain_mesh->vertices[ia];
        const render_terrain_vertex& vb = terrain_mesh->vertices[ib];
        const render_terrain_vertex& vc = terrain_mesh->vertices[ic];
        const glm::vec3 average_color = (va.color + vb.color + vc.color) / 3.0f;
        const glm::vec3 ink = average_color * 0.72f + glm::vec3(0.10f, 0.08f, 0.04f);
        draw_filled_map_triangle(batch,
                                 layout,
                                 map_point(layout, va.position),
                                 map_point(layout, vb.position),
                                 map_point(layout, vc.position),
                                 ink);
    }
}

void draw_paper_course_map(overlay_batch& batch, const render_data& data) {
    const course_map_layout layout = make_course_map_layout(data);
    const glm::vec3 paper(0.76f, 0.72f, 0.55f);
    const glm::vec3 paper_shadow(0.14f, 0.12f, 0.09f);
    draw_overlay_quad(batch, layout.center + glm::vec2(0.035f, -0.035f), layout.half_size, paper_shadow, 0.42f);
    draw_overlay_quad(batch, layout.center, layout.half_size, paper, 0.96f);

    const glm::vec3 fold(0.42f, 0.35f, 0.24f);
    draw_overlay_quad(batch, layout.center, glm::vec2(layout.half_size.x, 0.006f), fold, 0.18f);
    draw_overlay_quad(batch, layout.center, glm::vec2(0.006f, layout.half_size.y), fold, 0.18f);
    draw_overlay_quad(batch,
                      layout.center + glm::vec2(0.0f, layout.half_size.y),
                      glm::vec2(layout.half_size.x, 0.012f),
                      fold,
                      0.46f);
    draw_overlay_quad(batch,
                      layout.center - glm::vec2(0.0f, layout.half_size.y),
                      glm::vec2(layout.half_size.x, 0.012f),
                      fold,
                      0.46f);
    draw_overlay_quad(batch,
                      layout.center + glm::vec2(layout.half_size.x, 0.0f),
                      glm::vec2(0.012f, layout.half_size.y),
                      fold,
                      0.46f);
    draw_overlay_quad(batch,
                      layout.center - glm::vec2(layout.half_size.x, 0.0f),
                      glm::vec2(0.012f, layout.half_size.y),
                      fold,
                      0.46f);

    draw_filled_map_terrain(batch, layout, data);

    for (const render_tree& tree : data.trees) {
        const glm::vec2 p = map_point(layout, tree.base);
        const float radius = std::max(0.012f, std::min(0.028f, tree.leaf_radius * layout.scale));
        draw_map_marker(batch, p, glm::vec3(0.08f, 0.24f, 0.11f), radius);
    }

    draw_map_marker(batch, map_point(layout, data.tee_position), glm::vec3(0.34f, 0.21f, 0.12f), 0.023f);
    draw_map_marker(batch, map_point(layout, data.ball_position), glm::vec3(0.94f, 0.93f, 0.82f), 0.020f);
    draw_map_marker(batch, map_point(layout, data.player_position), glm::vec3(0.22f, 0.46f, 0.72f), 0.024f);

    const glm::vec2 pin = map_point(layout, data.pin_position);
    draw_overlay_quad(batch, pin + glm::vec2(0.0f, 0.028f), glm::vec2(0.004f, 0.042f), glm::vec3(0.06f, 0.04f, 0.025f), 0.92f);
    draw_overlay_quad(batch, pin + glm::vec2(0.022f, 0.055f), glm::vec2(0.028f, 0.018f), glm::vec3(0.76f, 0.17f, 0.12f), 0.96f);
    draw_map_marker(batch, pin, glm::vec3(0.94f, 0.78f, 0.22f), 0.018f);
}

std::string int_label(const int value) {
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    return std::string(buffer);
}

std::string score_label(const scorecard_row& row) {
    return row.played ? int_label(row.strokes) : "";
}

std::string relative_label(const scorecard_row& row) {
    return row.played ? row.relative_label : "";
}

std::string hole_label(const scorecard_row& row, const bool compact) {
    if (compact || row.hole_name.empty()) {
        return int_label(row.hole_number);
    }
    return int_label(row.hole_number) + " " + row.hole_name;
}

void draw_paper_card_base(overlay_batch& batch,
                          const glm::vec2 center,
                          const glm::vec2 half_size,
                          const float alpha) {
    const glm::vec3 paper(0.78f, 0.73f, 0.56f);
    const glm::vec3 paper_shadow(0.12f, 0.095f, 0.065f);
    const glm::vec3 fold(0.42f, 0.35f, 0.24f);

    draw_overlay_quad(batch, center + glm::vec2(0.030f, -0.034f), half_size, paper_shadow, 0.40f * alpha);
    draw_overlay_quad(batch, center, half_size, paper, 0.97f * alpha);
    draw_overlay_quad(batch, center + glm::vec2(-half_size.x * 0.28f, 0.0f), glm::vec2(0.004f, half_size.y), fold, 0.14f * alpha);
    draw_overlay_quad(batch, center + glm::vec2(half_size.x * 0.22f, 0.0f), glm::vec2(0.003f, half_size.y), fold, 0.10f * alpha);
    draw_button_outline(batch, center, half_size, fold, 0.44f * alpha);
}

void draw_scorecard_grid_lines(overlay_batch& batch,
                               const glm::vec2 center,
                               const glm::vec2 half_size,
                               const std::array<float, 5>& x_edges,
                               const float header_y,
                               const float row_height,
                               const int row_count,
                               const float alpha) {
    const glm::vec3 ink(0.23f, 0.19f, 0.13f);
    const float top = header_y + row_height * 0.62f;
    const float bottom = header_y - row_height * (static_cast<float>(row_count) + 0.62f);
    for (float x : x_edges) {
        draw_overlay_segment(batch, glm::vec2(x, top), glm::vec2(x, bottom), 0.004f, ink, 0.38f * alpha);
    }
    for (int i = 0; i <= row_count + 1; ++i) {
        const float y = top - static_cast<float>(i) * row_height;
        draw_overlay_segment(batch, glm::vec2(x_edges.front(), y), glm::vec2(x_edges.back(), y), 0.004f, ink, 0.34f * alpha);
    }
    draw_button_outline(batch, center, half_size, ink, 0.22f * alpha);
}

void draw_scorecard_headers(overlay_batch& batch,
                            const std::array<float, 5>& x_edges,
                            const float y,
                            const float pixel,
                            const glm::vec3 color) {
    draw_pixel_text_centered(batch, "HOLE", glm::vec2((x_edges[0] + x_edges[1]) * 0.5f, y), pixel, color);
    draw_pixel_text_centered(batch, "PAR", glm::vec2((x_edges[1] + x_edges[2]) * 0.5f, y), pixel, color);
    draw_pixel_text_centered(batch, "SCORE", glm::vec2((x_edges[2] + x_edges[3]) * 0.5f, y), pixel, color);
    draw_pixel_text_centered(batch, "+/-", glm::vec2((x_edges[3] + x_edges[4]) * 0.5f, y), pixel, color);
}

void draw_scorecard_row_text(overlay_batch& batch,
                             const scorecard_row& row,
                             const std::array<float, 5>& x_edges,
                             const float y,
                             const float pixel,
                             const bool compact,
                             const bool current) {
    const glm::vec3 ink = current ? glm::vec3(0.46f, 0.16f, 0.11f) : glm::vec3(0.16f, 0.12f, 0.08f);
    const glm::vec3 pending(0.40f, 0.35f, 0.25f);
    const glm::vec3 score_ink = row.played ? ink : pending;
    const std::string hole = hole_label(row, compact);
    const float hole_pixel = fit_pixel_size(hole,
                                            glm::vec2((x_edges[1] - x_edges[0]) * 0.48f, 0.030f),
                                            pixel,
                                            0.0055f);
    draw_pixel_text_left(batch, hole, glm::vec2(x_edges[0] + 0.014f, y + 3.5f * hole_pixel), hole_pixel, ink);
    draw_pixel_text_centered(batch, int_label(row.par), glm::vec2((x_edges[1] + x_edges[2]) * 0.5f, y), pixel, ink);
    draw_pixel_text_centered(batch, score_label(row), glm::vec2((x_edges[2] + x_edges[3]) * 0.5f, y), pixel, score_ink);
    draw_pixel_text_centered(batch, relative_label(row), glm::vec2((x_edges[3] + x_edges[4]) * 0.5f, y), pixel, score_ink);
}

void draw_scorecard_totals(overlay_batch& batch,
                           const scorecard_data& scorecard,
                           const std::array<float, 5>& x_edges,
                           const float y,
                           const float pixel) {
    const glm::vec3 ink(0.12f, 0.09f, 0.06f);
    draw_pixel_text_left(batch, "TOTAL", glm::vec2(x_edges[0] + 0.014f, y + 3.5f * pixel), pixel, ink);
    draw_pixel_text_centered(batch, int_label(scorecard.total_par), glm::vec2((x_edges[1] + x_edges[2]) * 0.5f, y), pixel, ink);
    draw_pixel_text_centered(batch, int_label(scorecard.total_strokes), glm::vec2((x_edges[2] + x_edges[3]) * 0.5f, y), pixel, ink);
    draw_pixel_text_centered(batch, scorecard.total_relative_label, glm::vec2((x_edges[3] + x_edges[4]) * 0.5f, y), pixel, ink);
}

void draw_scorecard_card(overlay_batch& batch,
                         const scorecard_data& scorecard,
                         const glm::vec2 center,
                         const glm::vec2 half_size,
                         const bool compact) {
    if (scorecard.rows.empty()) {
        return;
    }

    draw_paper_card_base(batch, center, half_size, 1.0f);

    const glm::vec3 title_color(0.20f, 0.12f, 0.06f);
    const glm::vec3 muted(0.35f, 0.30f, 0.20f);
    const float title_pixel = fit_pixel_size(scorecard.course_name, glm::vec2(half_size.x * 0.78f, 0.055f), compact ? 0.014f : 0.020f, 0.007f);
    draw_pixel_text_centered(batch, scorecard.course_name, center + glm::vec2(0.0f, half_size.y - 0.070f), title_pixel, title_color);
    draw_pixel_text_centered(batch,
                             compact ? "SCORECARD" : "COURSE RESULTS",
                             center + glm::vec2(0.0f, half_size.y - (compact ? 0.126f : 0.142f)),
                             compact ? 0.010f : 0.012f,
                             muted);

    const std::size_t row_limit = compact ? std::min<std::size_t>(scorecard.rows.size(), 8U) : scorecard.rows.size();
    std::size_t first_row = 0;
    if (compact && scorecard.rows.size() > row_limit) {
        const std::size_t current = std::min(scorecard.current_hole_index, scorecard.rows.size() - 1U);
        const std::size_t preferred = current > 3U ? current - 3U : 0U;
        first_row = std::min(preferred, scorecard.rows.size() - row_limit);
    }

    const float grid_top = center.y + half_size.y - (compact ? 0.180f : 0.220f);
    const float grid_bottom = center.y - half_size.y + (compact ? 0.118f : 0.142f);
    const float available = std::max(0.12f, grid_top - grid_bottom);
    const float row_height = std::min(compact ? 0.056f : 0.060f, available / static_cast<float>(row_limit + 1U));
    const float header_y = grid_top - row_height * 0.50f;
    const float x0 = center.x - half_size.x + 0.052f;
    const float x4 = center.x + half_size.x - 0.052f;
    const float hole_width = compact ? (x4 - x0) * 0.28f : (x4 - x0) * 0.46f;
    const float par_width = (x4 - x0) * 0.16f;
    const float score_width = (x4 - x0) * 0.22f;
    const std::array<float, 5> x_edges{{
        x0,
        x0 + hole_width,
        x0 + hole_width + par_width,
        x0 + hole_width + par_width + score_width,
        x4
    }};

    draw_scorecard_grid_lines(batch, center, half_size, x_edges, header_y, row_height, static_cast<int>(row_limit), 1.0f);
    draw_scorecard_headers(batch, x_edges, header_y, compact ? 0.0075f : 0.0090f, muted);

    const float row_pixel = std::min(compact ? 0.0090f : 0.0105f, row_height * 0.18f);
    for (std::size_t row_index = 0; row_index < row_limit; ++row_index) {
        const std::size_t source_index = first_row + row_index;
        const scorecard_row& row = scorecard.rows[source_index];
        const float y = header_y - row_height * (static_cast<float>(row_index) + 1.0f);
        const bool current = !scorecard.finished && source_index == scorecard.current_hole_index;
        draw_scorecard_row_text(batch, row, x_edges, y, row_pixel, compact, current);
    }

    const float total_y = center.y - half_size.y + (compact ? 0.060f : 0.076f);
    draw_scorecard_totals(batch, scorecard, x_edges, total_y, compact ? 0.0085f : 0.0105f);

    if (!compact) {
        draw_pixel_text_centered(batch,
                                 "ENTER / SPACE / ESC / BACKSPACE",
                                 center + glm::vec2(0.0f, -half_size.y + 0.034f),
                                 0.0070f,
                                 muted);
    }
}

void draw_compact_scorecard(overlay_batch& batch, const scorecard_data& scorecard) {
    draw_scorecard_card(batch, scorecard, glm::vec2(-0.48f, 0.32f), glm::vec2(0.44f, 0.44f), true);
}

void draw_course_results(overlay_batch& batch, const scorecard_data& scorecard) {
    draw_overlay_quad(batch, glm::vec2(0.0f), glm::vec2(1.0f), glm::vec3(0.015f, 0.013f, 0.012f), 0.70f);
    draw_scorecard_card(batch, scorecard, glm::vec2(0.0f, 0.0f), glm::vec2(0.74f, 0.80f), false);
}

void draw_stroke_ticks(overlay_batch& batch, const int stroke_count) {
    // These used to inherit u_alpha from the previous overlay quad; the club
    // label panel and its text (always drawn just before) leave it at 1.0.
    const int strokes = std::max(stroke_count, 0);
    const int ten_marks = std::min(strokes / 10, 8);
    for (int i = 0; i < ten_marks; ++i) {
        const float x = -0.92f + static_cast<float>(i) * 0.07f;
        draw_overlay_quad(batch, glm::vec2(x, 0.86f), glm::vec2(0.026f, 0.08f), glm::vec3(0.92f, 0.70f, 0.18f), 1.0f);
    }

    const int one_marks = strokes % 10;
    for (int i = 0; i < one_marks; ++i) {
        const float x = -0.92f + static_cast<float>(i) * 0.055f;
        draw_overlay_quad(batch, glm::vec2(x, 0.76f), glm::vec2(0.015f, 0.06f), glm::vec3(0.88f, 0.88f, 0.78f), 1.0f);
    }
}

// The debug overlay is flushed as its own draw so its GL cost can be moved
// into the DBGUI bucket: flush what is queued underneath it, mark, queue and
// flush the debug text, reclaim. Layering is unchanged; showing it costs one
// extra overlay draw call (the split) in the frame counters.
void draw_debug_overlay(overlay_pass& pass, const render_data& data, frame_profile* profile) {
    pass.flush();
    const debug_overlay_cost_mark mark = mark_debug_overlay_cost(profile);
    draw_fps_counter(pass.batch(), data.fps_label);
    draw_profile_overlay(pass.batch(), data.profile_summary);
    pass.flush();
    reclaim_debug_overlay_cost(profile, mark);
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

    const std::string tree_vert = asset_path("shaders/tree_instanced.vert");
    const std::string tree_frag = asset_path("shaders/terrain.frag");
    if (!tree_renderer_.init(tree_vert.c_str(),
                             tree_frag.c_str(),
                             tree_renderer::mesh_source{cylinder_vbo_, cylinder_vertex_count_},
                             tree_renderer::mesh_source{cone_vbo_, cone_vertex_count_})) {
        return false;
    }

    const std::string world_marker_vert = asset_path("shaders/world_marker.vert");
    const std::string world_marker_frag = asset_path("shaders/world_marker.frag");
    if (!world_marker_renderer_.init(world_marker_vert.c_str(), world_marker_frag.c_str())) {
        return false;
    }

    const std::string overlay_vert = asset_path("shaders/overlay.vert");
    const std::string overlay_frag = asset_path("shaders/overlay.frag");
    if (!overlay_pass_.init(overlay_vert.c_str(), overlay_frag.c_str())) {
        return false;
    }

    // Optional: profiling still works fully on the CPU side if this fails.
    gpu_timers_.init();

    return true;
}

void renderer::shutdown() {
    gpu_timers_.shutdown();
    tree_renderer_.shutdown();
    world_marker_renderer_.shutdown();
    overlay_pass_.shutdown();
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

    if (material_overlay_ebo_ != 0) {
        glDeleteBuffers(1, &material_overlay_ebo_);
        material_overlay_ebo_ = 0;
    }

    if (material_overlay_vbo_ != 0) {
        glDeleteBuffers(1, &material_overlay_vbo_);
        material_overlay_vbo_ = 0;
    }

    if (material_overlay_vao_ != 0) {
        glDeleteVertexArrays(1, &material_overlay_vao_);
        material_overlay_vao_ = 0;
    }

    if (ball_vbo_ != 0) {
        glDeleteBuffers(1, &ball_vbo_);
        ball_vbo_ = 0;
    }

    if (ball_vao_ != 0) {
        glDeleteVertexArrays(1, &ball_vao_);
        ball_vao_ = 0;
    }

    if (flight_path_vbo_ != 0) {
        glDeleteBuffers(1, &flight_path_vbo_);
        flight_path_vbo_ = 0;
    }

    if (flight_path_vao_ != 0) {
        glDeleteVertexArrays(1, &flight_path_vao_);
        flight_path_vao_ = 0;
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

void renderer::render(const render_data& data, frame_profile* profile) {
    if (!window_) {
        return;
    }

    const profile_scope render_timer(profile, profile_stage::render);
    terrain_shader_.set_profile(profile);
    ball_shader_.set_profile(profile);
    crt_shader_.set_profile(profile);
    gpu_timers_.begin_frame(profile != nullptr);

    int screen_width = 0;
    int screen_height = 0;
    SDL_GL_GetDrawableSize(window_, &screen_width, &screen_height);
    screen_width = std::max(1, screen_width);
    screen_height = std::max(1, screen_height);
    if (!ensure_framebuffer_size(screen_width, screen_height)) {
        return;
    }

    glEnable(GL_DEPTH_TEST);

    scene_fbo_.bind();
    glViewport(0, 0, target_width_, target_height_);
    glClearColor(0.36f, 0.56f, 0.82f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const glm::mat4 proj = glm::perspective(glm::radians(std::max(1.0f, data.camera_fov_degrees)),
                                            static_cast<float>(target_width_) / static_cast<float>(target_height_),
                                            0.1f,
                                            std::max(160.0f, data.course_extent * 2.5f));
    const glm::mat4 view = glm::lookAt(data.camera_position,
                                       data.camera_target,
                                       glm::vec3(0.0f, 1.0f, 0.0f));

    {
        const profile_scope scene_timer(profile, profile_stage::render_scene);
        render_scene(view, proj, data, profile);
    }

    {
        const profile_scope overlay_timer(profile, profile_stage::render_overlay);
        render_overlay(view, proj, data, profile);
    }

    framebuffer::bind_default();
    {
        const profile_scope crt_timer(profile, profile_stage::render_crt);
        render_crt(screen_width, screen_height, profile);
    }

    gpu_timers_.collect(profile);
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

    glGenVertexArrays(1, &material_overlay_vao_);
    glGenBuffers(1, &material_overlay_vbo_);
    glGenBuffers(1, &material_overlay_ebo_);
    glBindVertexArray(material_overlay_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, material_overlay_vbo_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, material_overlay_ebo_);
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

    glGenVertexArrays(1, &flight_path_vao_);
    glGenBuffers(1, &flight_path_vbo_);
    glBindVertexArray(flight_path_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, flight_path_vbo_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
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

bool renderer::ensure_framebuffer_size(const int screen_width, const int screen_height) {
    const float screen_aspect = static_cast<float>(std::max(1, screen_width)) /
        static_cast<float>(std::max(1, screen_height));
    const float reference_pixels = static_cast<float>(reference_low_res_width * reference_low_res_height);
    int next_width = std::max(min_low_res_dimension,
                              static_cast<int>(std::floor(std::sqrt(reference_pixels * screen_aspect) + 0.5f)));
    int next_height = static_cast<int>(std::floor(static_cast<float>(next_width) / screen_aspect + 0.5f));
    if (next_height < min_low_res_dimension) {
        next_height = min_low_res_dimension;
        next_width = static_cast<int>(std::floor(static_cast<float>(next_height) * screen_aspect + 0.5f));
    }

    if (next_width == target_width_ && next_height == target_height_ &&
        scene_fbo_.width() == target_width_ && scene_fbo_.height() == target_height_) {
        return true;
    }

    target_width_ = next_width;
    target_height_ = next_height;
    return init_framebuffer();
}

void renderer::upload_terrain_mesh(const render_data& data, frame_profile* profile) {
    const render_static_mesh* mesh = data.terrain_mesh;
    if (mesh == nullptr) {
        terrain_mesh_index_count_ = 0;
        terrain_mesh_uploaded_ = false;
        uploaded_terrain_revision_ = 0;
        return;
    }
    if (terrain_mesh_uploaded_ && uploaded_terrain_revision_ == mesh->revision) {
        return;
    }
    if (terrain_mesh_vao_ == 0 || terrain_mesh_vbo_ == 0 || terrain_mesh_ebo_ == 0 ||
        mesh->vertices.empty() || mesh->indices.empty()) {
        terrain_mesh_index_count_ = 0;
        terrain_mesh_uploaded_ = true;
        uploaded_terrain_revision_ = mesh->revision;
        return;
    }

    terrain_mesh_index_count_ = static_cast<int>(mesh->indices.size());

    glBindVertexArray(terrain_mesh_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, terrain_mesh_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh->vertices.size() * sizeof(render_terrain_vertex)),
                 mesh->vertices.data(),
                 GL_STATIC_DRAW);
    record_buffer_upload(profile, mesh->vertices.size() * sizeof(render_terrain_vertex));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, terrain_mesh_ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh->indices.size() * sizeof(std::uint32_t)),
                 mesh->indices.data(),
                 GL_STATIC_DRAW);
    record_buffer_upload(profile, mesh->indices.size() * sizeof(std::uint32_t));
    glBindVertexArray(0);
    terrain_mesh_uploaded_ = true;
    uploaded_terrain_revision_ = mesh->revision;
}

void renderer::upload_material_overlay_mesh(const render_data& data, frame_profile* profile) {
    const render_static_mesh* mesh = data.material_overlay_mesh;
    if (mesh == nullptr) {
        material_overlay_index_count_ = 0;
        material_overlay_mesh_uploaded_ = false;
        uploaded_material_overlay_revision_ = 0;
        return;
    }
    if (material_overlay_mesh_uploaded_ && uploaded_material_overlay_revision_ == mesh->revision) {
        return;
    }
    if (material_overlay_vao_ == 0 || material_overlay_vbo_ == 0 || material_overlay_ebo_ == 0 ||
        mesh->vertices.empty() || mesh->indices.empty()) {
        material_overlay_index_count_ = 0;
        material_overlay_mesh_uploaded_ = true;
        uploaded_material_overlay_revision_ = mesh->revision;
        return;
    }

    material_overlay_index_count_ = static_cast<int>(mesh->indices.size());

    glBindVertexArray(material_overlay_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, material_overlay_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh->vertices.size() * sizeof(render_terrain_vertex)),
                 mesh->vertices.data(),
                 GL_STATIC_DRAW);
    record_buffer_upload(profile, mesh->vertices.size() * sizeof(render_terrain_vertex));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, material_overlay_ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh->indices.size() * sizeof(std::uint32_t)),
                 mesh->indices.data(),
                 GL_STATIC_DRAW);
    record_buffer_upload(profile, mesh->indices.size() * sizeof(std::uint32_t));
    glBindVertexArray(0);
    material_overlay_mesh_uploaded_ = true;
    uploaded_material_overlay_revision_ = mesh->revision;
}

void renderer::render_scene(const glm::mat4& view, const glm::mat4& proj, const render_data& data, frame_profile* profile) {
    upload_terrain_mesh(data, profile);
    upload_material_overlay_mesh(data, profile);

    const float course_scale = std::max(1.0f, data.course_extent / 12.0f);
    const float terrain_min_y = render_mesh_min_y_or_zero(data.terrain_mesh);
    const float background_ground_y = std::min(-0.08f, terrain_min_y - 2.0f);
    const glm::mat4 ground_model = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, background_ground_y, 0.0f)),
                                              glm::vec3(course_scale, 1.0f, course_scale));

    gpu_timers_.begin(gpu_profile_stage::terrain);
    terrain_shader_.use();
    terrain_shader_.set_vec3("u_light_dir", glm::normalize(glm::vec3(-0.35f, 0.80f, 0.42f)));
    set_terrain_draw_state(terrain_shader_, ground_model, view, proj, glm::vec3(0.10f, 0.26f, 0.13f), false);

    glBindVertexArray(ground_vao_);
    draw_arrays(terrain_shader_, GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    // Frustum culling of the chunked static meshes. The chunk lists were built
    // with the meshes (app::refresh_render_mesh_cache) and describe the index
    // buffer that was just uploaded for this revision.
    const view_frustum frustum = make_view_frustum(proj * view);
    cull_stats_ = renderer_cull_stats{};

    if (terrain_mesh_index_count_ > 0) {
        cull_stats_.terrain = collect_visible_index_ranges(chunks_of(data.terrain_mesh),
                                                           frustum,
                                                           static_cast<std::size_t>(terrain_mesh_index_count_),
                                                           max_terrain_draw_ranges,
                                                           terrain_draw_ranges_);
        if (!terrain_draw_ranges_.empty()) {
            set_terrain_draw_state(terrain_shader_, glm::mat4(1.0f), view, proj, glm::vec3(0.18f, 0.42f, 0.18f), true);
            glBindVertexArray(terrain_mesh_vao_);
            draw_index_ranges(terrain_shader_, terrain_draw_ranges_);
            glBindVertexArray(0);
        }
    }

    if (material_overlay_index_count_ > 0) {
        cull_stats_.material_overlay = collect_visible_index_ranges(chunks_of(data.material_overlay_mesh),
                                                                    frustum,
                                                                    static_cast<std::size_t>(material_overlay_index_count_),
                                                                    max_material_overlay_draw_ranges,
                                                                    material_overlay_draw_ranges_);
        if (!material_overlay_draw_ranges_.empty()) {
            set_terrain_draw_state(terrain_shader_, glm::mat4(1.0f), view, proj, glm::vec3(1.0f), true);
            glBindVertexArray(material_overlay_vao_);
            draw_index_ranges(terrain_shader_, material_overlay_draw_ranges_);
            glBindVertexArray(0);
        }
    }

    gpu_timers_.end();

    gpu_timers_.begin(gpu_profile_stage::trees);
    cull_stats_.trees_visible = tree_renderer_.draw(data.trees, data.trees_revision, view, proj, frustum, profile);
    gpu_timers_.end();

    const primitive_geometry primitives{
        screen_vao_,
        cylinder_vao_,
        ball_vao_,
        cylinder_vertex_count_,
        ball_vertex_count_
    };
    terrain_shader_.use();
    terrain_shader_.set_vec3("u_light_dir", glm::normalize(glm::vec3(-0.35f, 0.80f, 0.42f)));
    draw_cart_model(terrain_shader_, view, proj, data, primitives);
    draw_emote_world_model(terrain_shader_, view, proj, data, primitives);
    glBindVertexArray(0);

    world_marker_scene markers;
    markers.show_primary_hole_markers = data.show_primary_hole_markers;
    markers.tee_position = data.tee_position;
    markers.pin_position = data.pin_position;
    markers.start_markers = &data.start_markers;
    markers.tee_markers = &data.tee_markers;
    markers.pin_markers = &data.pin_markers;
    markers.cup_radius = data.cup_radius;
    markers.cup_visual_radius_meters = data.cup_visual_radius_meters;
    markers.pin_visual_height_meters = data.pin_visual_height_meters;
    markers.show_aim_indicator = data.show_aim_indicator;
    markers.aim_arc_points = &data.aim_arc_points;
    markers.show_swing_club = data.shot_addressing || data.swing_timing;
    markers.ball_position = data.ball_position;
    markers.ball_visual_radius_meters = data.ball_visual_radius_meters;
    markers.aim_angle = data.aim_angle;
    markers.swing_power = data.swing_power;
    build_world_marker_batch(world_marker_batch_, markers);
    world_marker_renderer_.draw(world_marker_batch_, proj * view, profile);

    if (data.show_flight_path && data.flight_path_points.size() > 1 && flight_path_vao_ != 0) {
        std::vector<render_terrain_vertex> path_vertices;
        path_vertices.reserve(data.flight_path_points.size());
        for (const glm::vec3& point : data.flight_path_points) {
            render_terrain_vertex vertex;
            vertex.position = point + glm::vec3(0.0f, 0.02f, 0.0f);
            vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            vertex.color = glm::vec3(1.0f);
            path_vertices.push_back(vertex);
        }

        glBindVertexArray(flight_path_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, flight_path_vbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(path_vertices.size() * sizeof(render_terrain_vertex)),
                     path_vertices.data(),
                     GL_DYNAMIC_DRAW);
        record_buffer_upload(profile, path_vertices.size() * sizeof(render_terrain_vertex));

        terrain_shader_.use();
        const glm::mat4 model(1.0f);
        terrain_shader_.set_mat4("u_model", model);
        terrain_shader_.set_mat4("u_mvp", proj * view * model);
        terrain_shader_.set_vec3("u_color", data.flight_path_color);
        terrain_shader_.set_float("u_alpha", data.flight_path_alpha);
        terrain_shader_.set_int("u_use_vertex_color", 0);
        terrain_shader_.set_vec3("u_light_dir", glm::vec3(0.0f, 1.0f, 0.0f));

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glLineWidth(std::max(1.0f, data.flight_path_width));
        draw_arrays(terrain_shader_, GL_LINE_STRIP, 0, static_cast<GLsizei>(path_vertices.size()));
        glLineWidth(1.0f);
        glDisable(GL_BLEND);
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
    draw_arrays(ball_shader_, GL_TRIANGLES, 0, ball_vertex_count_);
    glBindVertexArray(0);
}

void renderer::render_overlay(const glm::mat4& view, const glm::mat4& proj, const render_data& data, frame_profile* profile) {
    gpu_timers_.begin(gpu_profile_stage::overlay);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Every overlay primitive appends to one vertex stream in submission order
    // (per-vertex colour + alpha), so painter's-order blending is preserved
    // and the whole overlay normally goes out in a single draw call. Nothing
    // below touches GL state except the explicit flushes.
    overlay_batch& batch = overlay_pass_.begin(profile);

    if (data.show_course_results) {
        draw_course_results(batch, data.scorecard);
        if (data.show_fps) {
            draw_debug_overlay(overlay_pass_, data, profile);
        }
        overlay_pass_.flush();
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        gpu_timers_.end();
        return;
    }

    if (data.show_course_map) {
        draw_paper_course_map(batch, data);
    }

    if (data.show_scorecard) {
        draw_compact_scorecard(batch, data.scorecard);
    }

    if (data.show_skills_panel) {
        draw_skills_panel(batch, data.skills);
    }

    if (data.show_fps) {
        draw_debug_overlay(overlay_pass_, data, profile);
    }

    if (data.show_rangefinder) {
        draw_rangefinder_view(batch, view, proj, data);
    }

    draw_cart_hud(batch, data);
    draw_club_label(batch, data.selected_club_label);

    if (data.show_interact_prompt) {
        draw_interact_prompt(batch);
    }

    draw_xp_drops(batch, data.xp_drops);

    draw_controls_overlay(batch, data.controls);

    if (data.show_power_meter) {
        draw_power_meter(batch, data.swing_power);
    }

    draw_stroke_ticks(batch, data.stroke_count);

    draw_startup_menu(batch, data.startup_menu);

    overlay_pass_.flush();
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    gpu_timers_.end();
}

void renderer::render_crt(int screen_width, int screen_height, frame_profile* profile) {
    (void)profile;
    gpu_timers_.begin(gpu_profile_stage::crt);
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
    draw_arrays(crt_shader_, GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    gpu_timers_.end();
}
