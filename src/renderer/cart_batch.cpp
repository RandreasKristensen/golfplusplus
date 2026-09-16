#include "renderer/cart_batch.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec4.hpp>

#include <array>

#include "renderer/primitive_mesh.h"

namespace {
// Every cart piece went through the terrain shader's flat path with
// u_alpha = 1 and blending disabled, and wrote depth.
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

glm::vec3 camera_forward(const glm::vec3& camera_position, const glm::vec3& camera_target) {
    glm::vec3 forward = camera_target - camera_position;
    forward.y = 0.0f;
    return glm::normalize(glm::length(forward) > 0.0001f ? forward : glm::vec3(0.0f, 0.0f, 1.0f));
}
}

glm::vec3 camera_local_point(const glm::vec3& camera_position,
                             const glm::vec3& camera_target,
                             const glm::vec3& local) {
    const glm::vec3 forward = camera_forward(camera_position, camera_target);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(up, forward));
    return camera_position + right * local.x + up * local.y + forward * local.z;
}

glm::mat4 camera_local_model(const glm::vec3& camera_position,
                             const glm::vec3& camera_target,
                             const glm::vec3& local,
                             const glm::vec3& rotation,
                             const glm::vec3& scale) {
    const glm::vec3 forward = camera_forward(camera_position, camera_target);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(up, forward));
    const glm::vec3 position = camera_position + right * local.x + up * local.y + forward * local.z;

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

glm::mat4 local_panel_model(const glm::vec3& camera_position,
                            const glm::vec3& camera_target,
                            const glm::vec3& local,
                            const glm::vec3& rotation,
                            const glm::vec2& half_size) {
    return camera_local_model(camera_position, camera_target, local, rotation, glm::vec3(half_size, 1.0f));
}

glm::mat4 local_cylinder_model(const glm::vec3& camera_position,
                               const glm::vec3& camera_target,
                               const glm::vec3& local,
                               const glm::vec3& rotation,
                               const glm::vec3& scale) {
    // The unit cylinder spans y in [0, 1]; the old draw scaled first and then
    // translated half a unit down in the *scaled* frame, so the piece ends up
    // centred on the local point. Scale then translate, in that order.
    glm::mat4 model = camera_local_model(camera_position, camera_target, local, rotation, glm::vec3(1.0f));
    model = glm::scale(model, scale);
    return glm::translate(model, glm::vec3(0.0f, -0.5f, 0.0f));
}

glm::mat4 local_sphere_model(const glm::vec3& camera_position,
                             const glm::vec3& camera_target,
                             const glm::vec3& local,
                             const float radius) {
    return camera_local_model(camera_position, camera_target, local, glm::vec3(0.0f), glm::vec3(radius));
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
