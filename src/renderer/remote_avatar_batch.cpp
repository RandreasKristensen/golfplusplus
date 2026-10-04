#include "renderer/remote_avatar_batch.h"

#include "physics/vector_math.h"
#include "renderer/camera_local.h"
#include "renderer/cart_batch.h"
#include "renderer/primitive_mesh.h"

#include <array>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

namespace {
constexpr float opaque = 1.0f;
constexpr bool writes_depth = true;
// The figure's height over the eye height, and its parts as fractions of it.
constexpr float figure_height_scale = 1.08f;

const glm::vec3 trousers_color(0.16f, 0.15f, 0.13f);
const glm::vec3 skin_color(0.80f, 0.62f, 0.46f);
const glm::vec3 cap_color(0.86f, 0.84f, 0.76f);
const glm::vec3 shoe_color(0.06f, 0.06f, 0.05f);

// Washed-out camcorder colours: shirts, then group highlights.
const std::array<glm::vec3, 8> shirt_palette{{
    {0.72f, 0.24f, 0.20f}, {0.22f, 0.40f, 0.66f}, {0.84f, 0.70f, 0.26f}, {0.30f, 0.56f, 0.32f},
    {0.62f, 0.36f, 0.62f}, {0.86f, 0.52f, 0.24f}, {0.26f, 0.62f, 0.64f}, {0.82f, 0.80f, 0.74f},
}};
const std::array<glm::vec3, 5> highlight_palette{{
    {0.98f, 0.86f, 0.18f}, {0.20f, 0.92f, 0.96f}, {0.98f, 0.38f, 0.74f}, {0.52f, 0.98f, 0.30f}, {0.98f, 0.56f, 0.16f},
}};

constexpr std::size_t quad_vertex_count = 6;
constexpr std::size_t figure_cylinder_count = 7;  // legs, torso, arms, neck, cap
constexpr std::size_t figure_sphere_count = 3;    // head, shoes
constexpr std::size_t figure_quad_count = 1;      // cap brim
}

glm::vec3 remote_tint(const std::uint64_t player_id) {
    return shirt_palette[player_id % shirt_palette.size()];
}

glm::vec3 group_highlight(const std::uint64_t group_id) {
    return highlight_palette[group_id % highlight_palette.size()];
}

std::size_t remote_figure_vertex_count() {
    return figure_cylinder_count * make_cylinder_positions(primitive_cylinder_segments).size() +
           figure_sphere_count * make_sphere_positions(primitive_sphere_latitude_segments, primitive_sphere_longitude_segments).size() +
           figure_quad_count * quad_vertex_count;
}

void append_remote_avatar(world_marker_batch& batch, const render_remote_avatar& avatar, const float eye_height) {
    const glm::vec3 forward = yaw_direction(avatar.yaw);
    if (avatar.group_id != 0) {
        append_ground_marker(batch, avatar.position, 1.6f, group_highlight(avatar.group_id));
    }
    if (avatar.in_cart) {
        // The cart is built around a driver's eyes.
        const glm::vec3 eye = avatar.position + world_up * eye_height;
        append_cart_model(batch, true, eye, eye + forward);
        return;
    }

    // The figure's own frame: feet at the origin, facing along its yaw.
    const glm::vec3& feet = avatar.position;
    const glm::vec3 facing = feet + forward;
    const float h = eye_height * figure_height_scale;
    const auto cylinder = [&](const glm::vec3& centre, const glm::vec3& rotation, const glm::vec3& scale, const glm::vec3& color) {
        batch.append_cylinder(local_cylinder_model(feet, facing, centre * h, rotation, scale * h), color, opaque, writes_depth);
    };
    const auto sphere = [&](const glm::vec3& centre, const float radius, const glm::vec3& color) {
        batch.append_sphere(local_sphere_model(feet, facing, centre * h, radius * h), color, opaque, writes_depth);
    };

    const glm::vec3 shirt = remote_tint(avatar.player_id);
    const glm::vec3 cap = avatar.group_id != 0 ? group_highlight(avatar.group_id) : cap_color;
    cylinder(glm::vec3(-0.07f, 0.23f, 0.0f), glm::vec3(0.0f), glm::vec3(0.06f, 0.46f, 0.06f), trousers_color);
    cylinder(glm::vec3(0.07f, 0.23f, 0.0f), glm::vec3(0.0f), glm::vec3(0.06f, 0.46f, 0.06f), trousers_color);
    sphere(glm::vec3(-0.07f, 0.05f, 0.03f), 0.05f, shoe_color);
    sphere(glm::vec3(0.07f, 0.05f, 0.03f), 0.05f, shoe_color);
    cylinder(glm::vec3(0.0f, 0.63f, 0.0f), glm::vec3(0.0f), glm::vec3(0.15f, 0.36f, 0.11f), shirt);
    cylinder(glm::vec3(-0.19f, 0.62f, 0.0f), glm::vec3(0.0f, 0.0f, glm::radians(-8.0f)), glm::vec3(0.045f, 0.32f, 0.045f), shirt);
    cylinder(glm::vec3(0.19f, 0.62f, 0.0f), glm::vec3(0.0f, 0.0f, glm::radians(8.0f)), glm::vec3(0.045f, 0.32f, 0.045f), shirt);
    cylinder(glm::vec3(0.0f, 0.83f, 0.0f), glm::vec3(0.0f), glm::vec3(0.04f, 0.06f, 0.04f), skin_color);
    sphere(glm::vec3(0.0f, 0.90f, 0.0f), 0.085f, skin_color);
    cylinder(glm::vec3(0.0f, 0.965f, 0.0f), glm::vec3(0.0f), glm::vec3(0.09f, 0.04f, 0.09f), cap);
    batch.append_quad(local_panel_model(feet, facing, glm::vec3(0.0f, 0.95f, 0.11f) * h,
                                        glm::vec3(glm::radians(90.0f), 0.0f, 0.0f), glm::vec2(0.07f, 0.05f) * h),
                      cap, opaque, writes_depth);
}

void append_remote_ball(world_marker_batch& batch, const render_remote_ball& ball, const float radius) {
    // Tinted towards the player's colour, still mostly white so it reads as a ball.
    const glm::vec3 color = glm::vec3(0.86f) * 0.6f + remote_tint(ball.player_id) * 0.4f;
    batch.append_sphere(glm::scale(glm::translate(glm::mat4(1.0f), ball.position), glm::vec3(radius)), color, opaque,
                        writes_depth);
}
