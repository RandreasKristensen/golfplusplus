#include "renderer/world_marker_batch.h"

#include "renderer/cart_batch.h"
#include "physics/vector_math.h"
#include "renderer/primitive_mesh.h"
#include "renderer/remote_avatar_batch.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace {
const glm::vec3 tee_color(0.45f, 0.30f, 0.16f);
const glm::vec3 start_marker_color(0.82f, 0.68f, 0.28f);
const glm::vec3 collectible_color(0.36f, 0.78f, 0.86f);
const glm::vec3 cup_color(0.03f, 0.03f, 0.035f);
const glm::vec3 pin_pole_color(0.95f, 0.90f, 0.68f);
const glm::vec3 flag_color(0.96f, 0.78f, 0.20f);
const glm::vec3 aim_dot_color(0.95f, 0.78f, 0.22f);

constexpr float hole_tee_scale = 1.8f;
constexpr float start_marker_scale = 2.2f;
constexpr float hub_tee_scale = 1.45f;
constexpr float collectible_scale = 1.2f;
constexpr float opaque = 1.0f;

float axis_x_yaw_radians(const glm::vec3& axis) {
    glm::vec3 flat(axis.x, 0.0f, axis.z);
    if (glm::length(flat) <= 0.0001f) {
        flat = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
        flat = glm::normalize(flat);
    }
    return std::atan2(-flat.z, flat.x);
}

glm::vec3 rotate_top_down_ccw_90_y(const glm::vec3& axis) {
    glm::vec3 rotated(-axis.z, 0.0f, axis.x);
    if (glm::length(rotated) <= 0.0001f) {
        return glm::vec3(1.0f, 0.0f, 0.0f);
    }
    return glm::normalize(rotated);
}
}

std::vector<glm::vec3> make_unit_disc_positions(const int segments) {
    std::vector<glm::vec3> positions;
    if (segments <= 0) {
        return positions;
    }
    positions.reserve(static_cast<std::size_t>(segments) * 3U);

    constexpr float radius = 0.5f;
    for (int i = 0; i < segments; ++i) {
        const float a0 = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = glm::two_pi<float>() * static_cast<float>(i + 1) / static_cast<float>(segments);
        positions.emplace_back(0.0f, 0.0f, 0.0f);
        positions.emplace_back(std::cos(a0) * radius, 0.0f, std::sin(a0) * radius);
        positions.emplace_back(std::cos(a1) * radius, 0.0f, std::sin(a1) * radius);
    }
    return positions;
}

std::vector<glm::vec3> make_unit_quad_positions() {
    return {
        glm::vec3(-1.0f, -1.0f, 0.0f),
        glm::vec3(1.0f, -1.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 0.0f),
        glm::vec3(-1.0f, -1.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 0.0f),
        glm::vec3(-1.0f, 1.0f, 0.0f),
    };
}

world_marker_batch::world_marker_batch()
    : unit_disc_(make_unit_disc_positions(world_marker_disc_segments)),
      unit_quad_(make_unit_quad_positions()),
      unit_cylinder_(make_cylinder_positions(primitive_cylinder_segments)),
      unit_sphere_(make_sphere_positions(primitive_sphere_latitude_segments,
                                         primitive_sphere_longitude_segments)) {}

void world_marker_batch::clear() {
    vertices_.clear();
    runs_.clear();
}

void world_marker_batch::append_disc(const glm::mat4& model,
                                     const glm::vec3& color,
                                     const float alpha,
                                     const bool depth_write) {
    append_triangles(unit_disc_, model, glm::vec4(color, alpha), depth_write);
}

void world_marker_batch::append_quad(const glm::mat4& model,
                                     const glm::vec3& color,
                                     const float alpha,
                                     const bool depth_write) {
    append_triangles(unit_quad_, model, glm::vec4(color, alpha), depth_write);
}

void world_marker_batch::append_cylinder(const glm::mat4& model,
                                         const glm::vec3& color,
                                         const float alpha,
                                         const bool depth_write) {
    append_triangles(unit_cylinder_, model, glm::vec4(color, alpha), depth_write);
}

void world_marker_batch::append_sphere(const glm::mat4& model,
                                       const glm::vec3& color,
                                       const float alpha,
                                       const bool depth_write) {
    append_triangles(unit_sphere_, model, glm::vec4(color, alpha), depth_write);
}

void world_marker_batch::append_triangles(const std::vector<glm::vec3>& local_positions,
                                          const glm::mat4& model,
                                          const glm::vec4& color,
                                          const bool depth_write) {
    if (local_positions.empty()) {
        return;
    }

    if (runs_.empty() || runs_.back().depth_write != depth_write) {
        world_marker_run run;
        run.depth_write = depth_write;
        run.first = vertices_.size();
        runs_.push_back(run);
    }

    for (const glm::vec3& local : local_positions) {
        world_marker_vertex vertex;
        vertex.position = glm::vec3(model * glm::vec4(local, 1.0f));
        vertex.color = color;
        vertices_.push_back(vertex);
    }
    runs_.back().count += local_positions.size();
}

glm::mat4 ground_marker_model(const glm::vec3& position, const float scale) {
    return glm::scale(glm::translate(glm::mat4(1.0f), position + glm::vec3(0.0f, 0.01f, 0.0f)),
                      glm::vec3(scale, 1.0f, scale));
}

glm::mat4 pin_cup_model(const glm::vec3& position, const float cup_scale) {
    return glm::scale(glm::translate(glm::mat4(1.0f), position + glm::vec3(0.0f, 0.09f, 0.0f)),
                      glm::vec3(cup_scale, 1.0f, cup_scale));
}

glm::mat4 aim_dot_model(const glm::vec3& point, const std::size_t index) {
    const float scale = 0.35f + static_cast<float>(index % 3) * 0.04f;
    return glm::scale(glm::translate(glm::mat4(1.0f), point + glm::vec3(0.0f, 0.05f, 0.0f)),
                      glm::vec3(scale, 1.0f, scale));
}

glm::mat4 panel_model(const glm::vec3& center, const float yaw_degrees, const glm::vec3& scale) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
    model = glm::rotate(model, glm::radians(yaw_degrees), glm::vec3(0.0f, 1.0f, 0.0f));
    return glm::scale(model, scale);
}

glm::mat4 world_panel_model(const glm::vec3& center,
                            const glm::vec3& axis_x,
                            const float local_z_rotation,
                            const glm::vec2& half_size) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
    model = glm::rotate(model, axis_x_yaw_radians(axis_x), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, local_z_rotation, glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::scale(model, glm::vec3(half_size, 1.0f));
}

void append_ground_marker(world_marker_batch& batch,
                          const glm::vec3& position,
                          const float scale,
                          const glm::vec3& color) {
    batch.append_disc(ground_marker_model(position, scale), color, opaque, true);
}

void append_pin_cup(world_marker_batch& batch, const glm::vec3& position, const float cup_radius_meters) {
    // The cup never writes depth so the ball and flagstick stay visible through it.
    batch.append_disc(pin_cup_model(position, cup_radius_meters * 2.0f), cup_color, opaque, false);
}

void append_pin_flagstick(world_marker_batch& batch, const glm::vec3& position, const float pin_visual_height_meters) {
    const float pin_height = std::max(0.1f, pin_visual_height_meters);
    const glm::vec3 pin_base = position + glm::vec3(0.0f, pin_height * 0.5f, 0.0f);
    const glm::vec3 pole_scale(0.045f, pin_height * 0.5f, 1.0f);
    batch.append_quad(panel_model(pin_base, 0.0f, pole_scale), pin_pole_color, opaque, true);
    batch.append_quad(panel_model(pin_base, 90.0f, pole_scale), pin_pole_color, opaque, true);

    const glm::vec3 flag_center = position + glm::vec3(0.34f, pin_height * 0.86f, 0.0f);
    const glm::vec3 flag_scale(0.36f, 0.24f, 1.0f);
    batch.append_quad(panel_model(flag_center, 0.0f, flag_scale), flag_color, opaque, true);
    batch.append_quad(panel_model(flag_center, 90.0f, flag_scale), flag_color, opaque, true);
}

void append_aim_dots(world_marker_batch& batch, const std::vector<glm::vec3>& points) {
    for (std::size_t i = 0; i < points.size(); ++i) {
        batch.append_disc(aim_dot_model(points[i], i), aim_dot_color, opaque, true);
    }
}

void append_swing_club(world_marker_batch& batch,
                       const glm::vec3& ball_position,
                       const float ball_visual_radius_meters,
                       const float aim_angle,
                       const float swing_power) {
    const float power = std::clamp(swing_power, 0.0f, 1.0f);
    const glm::vec3 forward = yaw_direction(aim_angle);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    glm::vec3 player_side = glm::normalize(glm::cross(up, forward));
    if (glm::length(player_side) <= 0.0001f) {
        player_side = glm::vec3(1.0f, 0.0f, 0.0f);
    }

    const glm::vec3 club_swing_side = -rotate_top_down_ccw_90_y(player_side);
    const glm::vec3 club_head_axis = rotate_top_down_ccw_90_y(forward);
    const glm::vec3 club_face_axis = club_head_axis;

    const float ball_radius = std::max(0.02f, ball_visual_radius_meters);
    const float shaft_length = 1.10f;
    const float swing_angle = glm::radians(12.0f + power * 60.0f);

    const glm::vec3 grip_position = ball_position
        + club_swing_side * (ball_radius + 0.10f)
        - club_head_axis * 0.36f
        + up * (shaft_length * 0.92f + ball_radius * 0.35f);

    const glm::vec3 shaft_direction = glm::normalize(club_swing_side * std::sin(swing_angle) -
                                                     up * std::cos(swing_angle));
    const glm::vec3 shaft_center = grip_position + shaft_direction * (shaft_length * 0.5f);
    const glm::vec3 head_center = grip_position + shaft_direction * shaft_length;

    batch.append_quad(world_panel_model(shaft_center, club_swing_side, swing_angle, glm::vec2(0.018f, shaft_length * 0.5f)),
                      glm::vec3(0.82f, 0.78f, 0.62f),
                      opaque,
                      true);
    batch.append_quad(world_panel_model(head_center + club_head_axis * 0.03f, club_face_axis, 0.0f, glm::vec2(0.16f, 0.040f)),
                      glm::vec3(0.16f, 0.15f, 0.13f),
                      opaque,
                      true);
    batch.append_quad(world_panel_model(head_center + club_head_axis * 0.055f, -club_face_axis, 0.0f, glm::vec2(0.14f, 0.045f)),
                      glm::vec3(0.20f, 0.19f, 0.17f),
                      opaque,
                      true);
    batch.append_quad(world_panel_model(head_center - club_swing_side * 0.02f, club_face_axis, 0.0f, glm::vec2(0.13f, 0.035f)),
                      glm::vec3(0.09f, 0.085f, 0.075f),
                      opaque,
                      true);
}

void build_world_marker_batch(world_marker_batch& batch, const world_marker_scene& scene) {
    batch.clear();
    append_cart_model(batch, scene.cart_active, scene.camera_position, scene.camera_target);

    if (scene.show_hole) {
        append_ground_marker(batch, scene.tee_position, hole_tee_scale, tee_color);
        append_pin_cup(batch, scene.pin_position, scene.cup_radius_meters);
        append_pin_flagstick(batch, scene.pin_position, scene.pin_visual_height_meters);
    }

    const std::vector<glm::vec3> none;
    const auto markers = [&none](const std::vector<glm::vec3>* positions) -> const std::vector<glm::vec3>& {
        return positions != nullptr ? *positions : none;
    };
    for (const glm::vec3& position : markers(scene.start_markers)) {
        append_ground_marker(batch, position, start_marker_scale, start_marker_color);
    }
    for (const glm::vec3& position : markers(scene.tee_markers)) {
        append_ground_marker(batch, position, hub_tee_scale, tee_color);
    }
    for (const glm::vec3& position : markers(scene.collectible_markers)) {
        append_ground_marker(batch, position, collectible_scale, collectible_color);
    }
    const std::vector<glm::vec3>& pins = markers(scene.pin_markers);
    for (const glm::vec3& position : pins) {
        append_pin_cup(batch, position, scene.cup_radius_meters);
    }
    for (const glm::vec3& position : pins) {
        append_pin_flagstick(batch, position, scene.pin_visual_height_meters);
    }

    if (scene.show_aim_indicator && scene.aim_arc_points != nullptr) {
        append_aim_dots(batch, *scene.aim_arc_points);
    }
    if (scene.show_swing_club) {
        append_swing_club(batch, scene.ball_position, scene.ball_visual_radius_meters, scene.aim_angle, scene.swing_power);
    }
    if (scene.remote_avatars != nullptr) {
        for (const render_remote_avatar& avatar : *scene.remote_avatars) {
            append_remote_avatar(batch, avatar, scene.avatar_eye_height);
        }
    }
    if (scene.remote_balls != nullptr) {
        for (const render_remote_ball& ball : *scene.remote_balls) {
            append_remote_ball(batch, ball, scene.ball_visual_radius_meters);
        }
    }
}
