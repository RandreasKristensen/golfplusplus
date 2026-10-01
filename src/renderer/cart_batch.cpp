#include "renderer/cart_batch.h"

#include <glm/trigonometric.hpp>

#include <array>

#include "renderer/camera_local.h"
#include "renderer/primitive_mesh.h"

namespace {
constexpr float opaque = 1.0f;
constexpr bool writes_depth = true;

const glm::vec3 body_color(0.36f, 0.46f, 0.20f);
const glm::vec3 trim_color(0.08f, 0.09f, 0.08f);
const glm::vec3 cream_color(0.76f, 0.72f, 0.56f);
const glm::vec3 canopy_color(0.72f, 0.68f, 0.47f);
const glm::vec3 rubber_color(0.025f, 0.025f, 0.025f);
const glm::vec3 hub_cap_color(0.58f, 0.56f, 0.48f);

constexpr std::size_t quad_vertex_count = 6;
constexpr std::size_t cart_quad_count = 6;
constexpr std::size_t cart_cylinder_count = 8;
constexpr std::size_t cart_sphere_count = 2;
}

std::size_t cart_model_vertex_count() {
    return cart_quad_count * quad_vertex_count +
           cart_cylinder_count * make_cylinder_positions(primitive_cylinder_segments).size() +
           cart_sphere_count * make_sphere_positions(primitive_sphere_latitude_segments,
                                                     primitive_sphere_longitude_segments).size();
}

void append_cart_model(world_marker_batch& batch,
                       const bool cart_active,
                       const glm::vec3& camera_position,
                       const glm::vec3& camera_target) {
    if (!cart_active) {
        return;
    }

    const auto panel = [&](const glm::vec3& local,
                           const glm::vec3& rotation,
                           const glm::vec2& half_size,
                           const glm::vec3& color) {
        batch.append_quad(local_panel_model(camera_position, camera_target, local, rotation, half_size),
                          color,
                          opaque,
                          writes_depth);
    };
    const auto cylinder = [&](const glm::vec3& local,
                              const glm::vec3& rotation,
                              const glm::vec3& scale,
                              const glm::vec3& color) {
        batch.append_cylinder(local_cylinder_model(camera_position, camera_target, local, rotation, scale),
                              color,
                              opaque,
                              writes_depth);
    };

    panel(glm::vec3(0.0f, -0.79f, 1.08f), glm::vec3(glm::radians(78.0f), 0.0f, 0.0f), glm::vec2(0.74f, 0.48f), body_color);
    panel(glm::vec3(0.0f, -0.63f, 0.58f), glm::vec3(glm::radians(82.0f), 0.0f, 0.0f), glm::vec2(0.68f, 0.18f), trim_color);
    panel(glm::vec3(0.0f, -0.50f, 0.72f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec2(0.64f, 0.13f), cream_color);
    panel(glm::vec3(-0.78f, -0.63f, 0.86f), glm::vec3(0.0f, glm::radians(90.0f), 0.0f), glm::vec2(0.42f, 0.16f), body_color);
    panel(glm::vec3(0.78f, -0.63f, 0.86f), glm::vec3(0.0f, glm::radians(90.0f), 0.0f), glm::vec2(0.42f, 0.16f), body_color);
    panel(glm::vec3(0.0f, 0.23f, 0.72f), glm::vec3(glm::radians(88.0f), 0.0f, 0.0f), glm::vec2(0.84f, 0.42f), canopy_color);
    cylinder(glm::vec3(0.0f, -0.63f, 0.40f),
             glm::vec3(glm::radians(68.0f), 0.0f, glm::radians(90.0f)),
             glm::vec3(0.22f, 0.035f, 0.22f),
             rubber_color);
    cylinder(glm::vec3(0.0f, -0.73f, 0.48f),
             glm::vec3(glm::radians(22.0f), 0.0f, 0.0f),
             glm::vec3(0.024f, 0.30f, 0.024f),
             trim_color);

    const std::array<float, 2> post_x{-0.56f, 0.56f};
    const std::array<float, 2> post_z{0.46f, 1.10f};
    for (const float x : post_x) {
        for (const float z : post_z) {
            cylinder(glm::vec3(x, -0.17f, z), glm::vec3(0.0f), glm::vec3(0.035f, 0.84f, 0.035f), cream_color);
        }
    }

    const std::array<float, 2> wheel_x{-0.68f, 0.68f};
    const std::array<float, 1> wheel_z{1.30f};
    for (const float x : wheel_x) {
        for (const float z : wheel_z) {
            cylinder(glm::vec3(x, -1.08f, z),
                     glm::vec3(0.0f, 0.0f, glm::radians(-90.0f)),
                     glm::vec3(0.23f, 0.16f, 0.23f),
                     rubber_color);
            batch.append_sphere(local_sphere_model(camera_position, camera_target, glm::vec3(x, -1.08f, z), 0.085f),
                                hub_cap_color,
                                opaque,
                                writes_depth);
        }
    }
}
