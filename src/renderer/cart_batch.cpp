#include "renderer/cart_batch.h"

#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

#include <array>
#include <cmath>

#include "physics/vector_math.h"
#include "renderer/camera_local.h"
#include "renderer/primitive_mesh.h"

namespace {
constexpr float no_glow = 0.0f;
constexpr bool writes_depth = true;

constexpr std::size_t quad_vertex_count = 6;
constexpr std::size_t box_count = 8;
constexpr std::size_t wheel_rim_segments = 12;
constexpr std::size_t wheel_spoke_count = 3;
// Posts, wheels, bag, steering column, the steering wheel's rim and spokes;
// hub caps and the steering wheel's hub.
constexpr std::size_t cylinder_count = 4 + 4 + 1 + 1 + wheel_rim_segments + wheel_spoke_count;
constexpr std::size_t sphere_count = 4 + 1;

// The steering wheel before the driver's seat, in the cart's frame: its
// middle, how far its face leans back from upright, and how far it turns at
// full steer.
constexpr float wheel_height = 1.06f;
constexpr float wheel_ahead = 0.46f;
constexpr float wheel_lean_degrees = 40.0f;
constexpr float wheel_radius = 0.19f;
constexpr float wheel_rim_radius = 0.017f;
constexpr float wheel_spoke_radius = 0.012f;
constexpr float wheel_hub_radius = 0.035f;
constexpr float wheel_column_length = 0.42f;
constexpr float wheel_column_radius = 0.024f;
constexpr float wheel_full_turn_degrees = 110.0f;
}

std::size_t cart_vertex_count() {
    return box_count * 6 * quad_vertex_count + cylinder_count * make_cylinder_positions(primitive_cylinder_segments).size() +
           sphere_count * make_sphere_positions(primitive_sphere_latitude_segments, primitive_sphere_longitude_segments).size();
}

void append_cart_model(world_marker_batch& batch, const render_cart& cart, const float seat_offset, const cart_colors& colors) {
    // The cart's frame (x towards its left, y up, z forward, world units):
    // the ground under the middle of its seat at the origin.
    const glm::vec3& feet = cart.position;
    const glm::vec3 facing = feet + yaw_direction(cart.yaw);
    const auto panel = [&](const glm::vec3& local, const glm::vec3& rotation, const glm::vec2& half_size,
                           const glm::vec3& color) {
        batch.append_quad(local_panel_model(feet, facing, local, rotation, half_size), color, no_glow, writes_depth);
    };
    const auto box = [&](const glm::vec3& centre, const glm::vec3& half, const glm::vec3& color) {
        const float quarter = glm::radians(90.0f);
        panel(centre + glm::vec3(0.0f, 0.0f, half.z), glm::vec3(0.0f), glm::vec2(half.x, half.y), color);
        panel(centre - glm::vec3(0.0f, 0.0f, half.z), glm::vec3(0.0f), glm::vec2(half.x, half.y), color);
        panel(centre + glm::vec3(half.x, 0.0f, 0.0f), glm::vec3(0.0f, quarter, 0.0f), glm::vec2(half.z, half.y), color);
        panel(centre - glm::vec3(half.x, 0.0f, 0.0f), glm::vec3(0.0f, quarter, 0.0f), glm::vec2(half.z, half.y), color);
        panel(centre + glm::vec3(0.0f, half.y, 0.0f), glm::vec3(quarter, 0.0f, 0.0f), glm::vec2(half.x, half.z), color);
        panel(centre - glm::vec3(0.0f, half.y, 0.0f), glm::vec3(quarter, 0.0f, 0.0f), glm::vec2(half.x, half.z), color);
    };
    const auto cylinder = [&](const glm::vec3& local, const glm::vec3& rotation, const glm::vec3& scale, const glm::vec3& color) {
        batch.append_cylinder(local_cylinder_model(feet, facing, local, rotation, scale), color, no_glow, writes_depth);
    };
    const auto rod = [&](const glm::vec3& from, const glm::vec3& to, const float radius, const glm::vec3& color) {
        batch.append_cylinder(local_segment_model(feet, facing, from, to, radius), color, no_glow, writes_depth);
    };

    box(glm::vec3(0.0f, 0.40f, 0.15f), glm::vec3(0.62f, 0.08f, 1.15f), colors.body);    // floor and frame
    box(glm::vec3(0.0f, 0.62f, 1.05f), glm::vec3(0.60f, 0.16f, 0.28f), colors.body);    // hood
    box(glm::vec3(0.0f, 0.86f, 0.80f), glm::vec3(0.58f, 0.08f, 0.06f), colors.cream);   // dash
    box(glm::vec3(0.0f, 0.36f, 1.34f), glm::vec3(0.64f, 0.06f, 0.04f), colors.trim);    // bumper
    box(glm::vec3(0.0f, 0.58f, -0.12f), glm::vec3(0.58f, 0.12f, 0.28f), colors.seat);   // seat
    box(glm::vec3(0.0f, 0.98f, -0.40f), glm::vec3(0.58f, 0.26f, 0.05f), colors.seat);   // seat back
    box(glm::vec3(0.0f, 0.62f, -0.78f), glm::vec3(0.62f, 0.14f, 0.24f), colors.body);   // rear deck
    box(glm::vec3(0.0f, 2.00f, 0.05f), glm::vec3(0.72f, 0.03f, 1.00f), colors.canopy);  // roof

    const std::array<float, 2> post_x{-0.56f, 0.56f};
    const std::array<float, 2> post_z{-0.62f, 0.72f};
    for (const float x : post_x) {
        for (const float z : post_z) {
            cylinder(glm::vec3(x, 1.23f, z), glm::vec3(0.0f), glm::vec3(0.03f, 1.50f, 0.03f), colors.cream);
        }
    }
    const std::array<float, 2> wheel_x{-0.66f, 0.66f};
    const std::array<float, 2> wheel_z{-0.70f, 1.00f};
    for (const float x : wheel_x) {
        for (const float z : wheel_z) {
            cylinder(glm::vec3(x, 0.23f, z), glm::vec3(0.0f, 0.0f, glm::radians(-90.0f)), glm::vec3(0.23f, 0.16f, 0.23f),
                     colors.rubber);
            batch.append_sphere(local_sphere_model(feet, facing, glm::vec3(x * 1.1f, 0.23f, z), 0.085f), colors.hub_cap, no_glow,
                                writes_depth);
        }
    }
    // A golf bag stood in the rear deck, leaning back.
    cylinder(glm::vec3(0.0f, 1.05f, -0.82f), glm::vec3(glm::radians(-20.0f), 0.0f, 0.0f), glm::vec3(0.14f, 0.75f, 0.14f),
             colors.bag);

    // The steering wheel faces the driver's chest on a column down into the
    // dash; across its face run `side` (the cart's left) and `rise` (up it).
    const float lean = glm::radians(wheel_lean_degrees);
    const glm::vec3 hub(seat_offset, wheel_height, wheel_ahead);
    const glm::vec3 towards_driver(0.0f, std::sin(lean), -std::cos(lean));
    const glm::vec3 side(1.0f, 0.0f, 0.0f);
    const glm::vec3 rise(0.0f, std::cos(lean), std::sin(lean));
    rod(hub - towards_driver * wheel_column_length, hub, wheel_column_radius, colors.trim);
    const float turn = -glm::clamp(cart.steer, -1.0f, 1.0f) * glm::radians(wheel_full_turn_degrees);
    const auto rim_point = [&](const float angle) {
        return hub + (side * std::cos(angle + turn) + rise * std::sin(angle + turn)) * wheel_radius;
    };
    const float pi = glm::pi<float>();
    const float rim_step = 2.0f * pi / static_cast<float>(wheel_rim_segments);
    for (std::size_t i = 0; i < wheel_rim_segments; ++i) {
        rod(rim_point(rim_step * static_cast<float>(i)), rim_point(rim_step * static_cast<float>(i + 1)), wheel_rim_radius,
            colors.rubber);
    }
    // Held straight, its spokes run out to either side and down.
    for (const float angle : std::array<float, wheel_spoke_count>{0.0f, pi, 1.5f * pi}) {
        rod(hub, rim_point(angle), wheel_spoke_radius, colors.trim);
    }
    batch.append_sphere(local_sphere_model(feet, facing, hub, wheel_hub_radius), colors.trim, no_glow, writes_depth);
}
