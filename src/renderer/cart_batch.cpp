#include "renderer/cart_batch.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "physics/vector_math.h"
#include "renderer/camera_local.h"
#include "renderer/primitive_mesh.h"
#include "renderer/prop_pieces.h"

namespace {
constexpr float no_glow = 0.0f;
constexpr bool writes_depth = true;

constexpr std::size_t quad_vertex_count = 6;
constexpr std::size_t wheel_rim_segments = 12;
constexpr std::size_t wheel_spoke_count = 3;
// Besides its model's parts: the wheels, steering column, the steering
// wheel's rim and spokes; hub caps and the steering wheel's hub.
constexpr std::size_t cylinder_count = 4 + 1 + wheel_rim_segments + wheel_spoke_count;
constexpr std::size_t sphere_count = 4 + 1;

// The wheels, in the cart's frame: their hubs at the wheel radius over its
// origin, `wheel_x` to either side and `wheel_z` behind and ahead.
constexpr std::array<float, 2> wheel_x{-0.66f, 0.66f};
constexpr std::array<float, 2> wheel_z{-0.70f, 1.00f};
constexpr float tyre_radius = 0.23f;
constexpr float tyre_width = 0.16f;
// The cart rests on the ground smoothed round each wheel, out to
// `ground_smoothing_radius` in `ground_smoothing_samples` directions, so it
// rides over small bumps and creases; its tyres float `tyre_clearance` over
// that, and never nearer the true ground than `tyre_least_clearance`, checked
// round each tyre's lower half at both its edges.
constexpr float ground_smoothing_radius = 0.7f;
constexpr std::size_t ground_smoothing_samples = 8;
constexpr float tyre_clearance = 0.04f;
constexpr float tyre_least_clearance = 0.01f;
constexpr std::array<float, 3> tyre_check_degrees{-45.0f, 0.0f, 45.0f};

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

std::size_t cart_vertex_count(const prop_model& model) {
    const std::size_t cylinder = make_cylinder_positions(primitive_cylinder_segments).size();
    const std::size_t sphere = make_sphere_positions(primitive_sphere_latitude_segments, primitive_sphere_longitude_segments).size();
    return prop_piece_count(model, prop_mesh::quad) * quad_vertex_count +
           (prop_piece_count(model, prop_mesh::cylinder) + cylinder_count) * cylinder +
           (prop_piece_count(model, prop_mesh::sphere) + sphere_count) * sphere;
}

glm::mat4 cart_frame(const render_cart& cart) {
    const glm::vec3 up = safe_normalize(cart.up, world_up);
    const glm::vec3 heading = yaw_direction(cart.yaw);
    const glm::vec3 forward = safe_normalize(heading - up * glm::dot(heading, up), heading);
    glm::mat4 frame(1.0f);
    frame[0] = glm::vec4(glm::normalize(glm::cross(up, forward)), 0.0f);
    frame[1] = glm::vec4(up, 0.0f);
    frame[2] = glm::vec4(forward, 0.0f);
    frame[3] = glm::vec4(cart.position, 1.0f);
    return frame;
}

render_cart cart_on_ground(const glm::vec3& position, const float yaw,
                           const std::function<float(const glm::vec3&)>& ground_height) {
    render_cart cart;
    cart.position = glm::vec3(position.x, 0.0f, position.z);
    cart.yaw = yaw;

    // Tilted along the smoothed rise from its rear wheels to its front ones
    // and from its right wheels to its left ones.
    const glm::mat4 level = cart_frame(cart);
    const auto smoothed_ground = [&](const glm::vec3& wheel) {
        float sum = ground_height(wheel);
        for (std::size_t i = 0; i < ground_smoothing_samples; ++i) {
            const float angle = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(ground_smoothing_samples);
            sum += ground_height(wheel + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * ground_smoothing_radius);
        }
        return sum / static_cast<float>(ground_smoothing_samples + 1);
    };
    std::array<std::array<float, 2>, 2> ground{};
    for (std::size_t side = 0; side < 2; ++side) {
        for (std::size_t end = 0; end < 2; ++end) {
            ground[side][end] = smoothed_ground(glm::vec3(level * glm::vec4(wheel_x[side], 0.0f, wheel_z[end], 1.0f)));
        }
    }
    const float rise_ahead = 0.5f * (ground[0][1] + ground[1][1] - ground[0][0] - ground[1][0]);
    const float rise_left = 0.5f * (ground[1][0] + ground[1][1] - ground[0][0] - ground[0][1]);
    const glm::vec3 along = glm::vec3(level[2]) * (wheel_z[1] - wheel_z[0]) + world_up * rise_ahead;
    const glm::vec3 across = glm::vec3(level[0]) * (wheel_x[1] - wheel_x[0]) + world_up * rise_left;
    cart.up = safe_normalize(glm::cross(along, across), world_up);

    // Its tyres at the clearance over the smoothed ground on average, lifted
    // further where the true ground would come nearer than the least.
    const glm::mat4 tilted = cart_frame(cart);
    float lift = 0.0f;
    for (std::size_t side = 0; side < 2; ++side) {
        for (std::size_t end = 0; end < 2; ++end) {
            const glm::vec3 bottom(tilted * glm::vec4(wheel_x[side], 0.0f, wheel_z[end], 1.0f));
            lift += 0.25f * (ground[side][end] + tyre_clearance - bottom.y);
        }
    }
    for (const float x : wheel_x) {
        for (const float z : wheel_z) {
            for (const float edge : {-0.5f * tyre_width, 0.5f * tyre_width}) {
                for (const float degrees : tyre_check_degrees) {
                    const float angle = glm::radians(degrees);
                    const glm::vec3 local(x + edge, tyre_radius * (1.0f - std::cos(angle)), z + tyre_radius * std::sin(angle));
                    const glm::vec3 point(tilted * glm::vec4(local, 1.0f));
                    lift = std::max(lift, ground_height(point) + tyre_least_clearance - point.y);
                }
            }
        }
    }
    cart.position.y = lift;
    return cart;
}

void append_cart_model(world_marker_batch& batch, const render_cart& cart, const float seat_offset, const prop_model& model) {
    // Built in the cart's frame (x towards its left, y up, z forward, world
    // units), under the middle of its seat at the bottom of its wheels, then
    // set in the world by cart_frame: its model's parts, then the wheels and
    // the steering wheel, which turn.
    const glm::mat4 frame = cart_frame(cart);
    for (const prop_piece& piece : prop_pieces(model, frame)) {
        switch (piece.mesh) {
        case prop_mesh::cylinder:
            batch.append_cylinder(piece.model, piece.color, no_glow, writes_depth);
            break;
        case prop_mesh::sphere:
            batch.append_sphere(piece.model, piece.color, no_glow, writes_depth);
            break;
        case prop_mesh::quad:
            batch.append_quad(piece.model, piece.color, no_glow, writes_depth);
            break;
        }
    }
    const glm::vec3 feet(0.0f);
    const glm::vec3 facing(0.0f, 0.0f, 1.0f);
    const glm::vec3 rubber = model.color("rubber");
    const glm::vec3 trim = model.color("trim");
    const auto cylinder = [&](const glm::vec3& local, const glm::vec3& rotation, const glm::vec3& scale, const glm::vec3& color) {
        batch.append_cylinder(frame * local_cylinder_model(feet, facing, local, rotation, scale), color, no_glow, writes_depth);
    };
    const auto rod = [&](const glm::vec3& from, const glm::vec3& to, const float radius, const glm::vec3& color) {
        batch.append_cylinder(frame * local_segment_model(feet, facing, from, to, radius), color, no_glow, writes_depth);
    };

    for (const float x : wheel_x) {
        for (const float z : wheel_z) {
            cylinder(glm::vec3(x, tyre_radius, z), glm::vec3(0.0f, 0.0f, glm::radians(-90.0f)),
                     glm::vec3(tyre_radius, tyre_width, tyre_radius), rubber);
            batch.append_sphere(frame * local_sphere_model(feet, facing, glm::vec3(x * 1.1f, tyre_radius, z), 0.085f),
                                model.color("hub_cap"), no_glow, writes_depth);
        }
    }

    // The steering wheel faces the driver's chest on a column down into the
    // dash; across its face run `side` (the cart's left) and `rise` (up it).
    const float lean = glm::radians(wheel_lean_degrees);
    const glm::vec3 hub(seat_offset, wheel_height, wheel_ahead);
    const glm::vec3 towards_driver(0.0f, std::sin(lean), -std::cos(lean));
    const glm::vec3 side(1.0f, 0.0f, 0.0f);
    const glm::vec3 rise(0.0f, std::cos(lean), std::sin(lean));
    rod(hub - towards_driver * wheel_column_length, hub, wheel_column_radius, trim);
    const float turn = -glm::clamp(cart.steer, -1.0f, 1.0f) * glm::radians(wheel_full_turn_degrees);
    const auto rim_point = [&](const float angle) {
        return hub + (side * std::cos(angle + turn) + rise * std::sin(angle + turn)) * wheel_radius;
    };
    const float pi = glm::pi<float>();
    const float rim_step = 2.0f * pi / static_cast<float>(wheel_rim_segments);
    for (std::size_t i = 0; i < wheel_rim_segments; ++i) {
        rod(rim_point(rim_step * static_cast<float>(i)), rim_point(rim_step * static_cast<float>(i + 1)), wheel_rim_radius,
            rubber);
    }
    // Held straight, its spokes run out to either side and down.
    for (const float angle : std::array<float, wheel_spoke_count>{0.0f, pi, 1.5f * pi}) {
        rod(hub, rim_point(angle), wheel_spoke_radius, trim);
    }
    batch.append_sphere(frame * local_sphere_model(feet, facing, hub, wheel_hub_radius), trim, no_glow, writes_depth);
}
