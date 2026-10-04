#include "doctest.h"

#include "renderer/camera_local.h"
#include "renderer/cart_batch.h"
#include "renderer/primitive_mesh.h"
#include "renderer/remote_avatar_batch.h"
#include "renderer/world_marker_batch.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {
constexpr std::size_t disc_vertex_count = 54;
constexpr std::size_t quad_vertex_count = 6;
constexpr std::size_t flagstick_vertex_count = 4 * quad_vertex_count;
constexpr std::size_t club_vertex_count = 4 * quad_vertex_count;

// An 18-segment disc fan, radius 0.5, as flat xyz floats.
std::vector<float> expected_disc_vertices() {
    std::vector<float> vertices;
    constexpr int segments = 18;
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

std::vector<glm::vec3> expected_disc_positions() {
    const std::vector<float> flat = expected_disc_vertices();
    std::vector<glm::vec3> positions;
    for (std::size_t i = 0; i + 2 < flat.size(); i += 3) {
        positions.emplace_back(flat[i], flat[i + 1], flat[i + 2]);
    }
    return positions;
}

// The unit quad: two triangles over [-1, 1] in XY.
std::vector<glm::vec3> expected_quad_positions() {
    return {
        glm::vec3(-1.0f, -1.0f, 0.0f),
        glm::vec3(1.0f, -1.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 0.0f),
        glm::vec3(-1.0f, -1.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 0.0f),
        glm::vec3(-1.0f, 1.0f, 0.0f),
    };
}

// Model matrices each marker piece must be drawn with.
glm::mat4 expected_ground_marker_model(const glm::vec3& position, const float scale) {
    return glm::scale(glm::translate(glm::mat4(1.0f), position + glm::vec3(0.0f, 0.01f, 0.0f)),
                      glm::vec3(scale, 1.0f, scale));
}

glm::mat4 expected_panel_model(const glm::vec3 center, const float yaw_degrees, const glm::vec3 scale) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
    model = glm::rotate(model, glm::radians(yaw_degrees), glm::vec3(0.0f, 1.0f, 0.0f));
    return glm::scale(model, scale);
}

float expected_axis_x_yaw_radians(const glm::vec3& axis) {
    glm::vec3 flat(axis.x, 0.0f, axis.z);
    if (glm::length(flat) <= 0.0001f) {
        flat = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
        flat = glm::normalize(flat);
    }
    return std::atan2(-flat.z, flat.x);
}

glm::vec3 expected_rotate_top_down_ccw_90_y(const glm::vec3& axis) {
    glm::vec3 rotated(-axis.z, 0.0f, axis.x);
    if (glm::length(rotated) <= 0.0001f) {
        return glm::vec3(1.0f, 0.0f, 0.0f);
    }
    return glm::normalize(rotated);
}

struct expected_panel {
    glm::mat4 model;
    glm::vec3 color;
};

glm::mat4 expected_world_panel_model(const glm::vec3& center,
                                const glm::vec3& axis_x,
                                const float local_z_rotation,
                                const glm::vec2& half_size) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
    model = glm::rotate(model, expected_axis_x_yaw_radians(axis_x), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, local_z_rotation, glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::scale(model, glm::vec3(half_size, 1.0f));
}

// The swing club as (model, color) pairs, in draw order.
std::vector<expected_panel> expected_swing_club(const glm::vec3& ball_position,
                                      const float ball_visual_radius_meters,
                                      const float aim_angle,
                                      const float swing_power) {
    const float power = std::clamp(swing_power, 0.0f, 1.0f);
    const glm::vec3 forward = glm::normalize(glm::vec3(std::sin(aim_angle), 0.0f, std::cos(aim_angle)));
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    glm::vec3 player_side = glm::normalize(glm::cross(up, forward));
    if (glm::length(player_side) <= 0.0001f) {
        player_side = glm::vec3(1.0f, 0.0f, 0.0f);
    }

    const glm::vec3 club_swing_side = -expected_rotate_top_down_ccw_90_y(player_side);
    const glm::vec3 club_head_axis = expected_rotate_top_down_ccw_90_y(forward);
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

    return {
        {expected_world_panel_model(shaft_center, club_swing_side, swing_angle, glm::vec2(0.018f, shaft_length * 0.5f)),
         glm::vec3(0.82f, 0.78f, 0.62f)},
        {expected_world_panel_model(head_center + club_head_axis * 0.03f, club_face_axis, 0.0f, glm::vec2(0.16f, 0.040f)),
         glm::vec3(0.16f, 0.15f, 0.13f)},
        {expected_world_panel_model(head_center + club_head_axis * 0.055f, -club_face_axis, 0.0f, glm::vec2(0.14f, 0.045f)),
         glm::vec3(0.20f, 0.19f, 0.17f)},
        {expected_world_panel_model(head_center - club_swing_side * 0.02f, club_face_axis, 0.0f, glm::vec2(0.13f, 0.035f)),
         glm::vec3(0.09f, 0.085f, 0.075f)},
    };
}

void check_near(const glm::vec3& actual, const glm::vec3& expected) {
    CHECK(std::abs(actual.x - expected.x) <= 1e-5f);
    CHECK(std::abs(actual.y - expected.y) <= 1e-5f);
    CHECK(std::abs(actual.z - expected.z) <= 1e-5f);
}

// Checks that vertices [first, first + local.size()) are expected_model * local
// with the given flat color and alpha. Returns the index after the piece.
std::size_t check_piece(const world_marker_batch& batch,
                        const std::size_t first,
                        const std::vector<glm::vec3>& local,
                        const glm::mat4& expected_model,
                        const glm::vec3& color,
                        const float alpha = 1.0f) {
    const std::vector<world_marker_vertex>& vertices = batch.vertices();
    CHECK(first + local.size() <= vertices.size());
    if (first + local.size() > vertices.size()) {
        return first + local.size();
    }
    for (std::size_t i = 0; i < local.size(); ++i) {
        const glm::vec4 expected = expected_model * glm::vec4(local[i], 1.0f);
        check_near(vertices[first + i].position, glm::vec3(expected));
        CHECK(vertices[first + i].color == glm::vec4(color, alpha));
    }
    return first + local.size();
}

void check_run(const world_marker_run& run, const bool depth_write, const std::size_t first, const std::size_t count) {
    CHECK(run.depth_write == depth_write);
    CHECK(run.first == first);
    CHECK(run.count == count);
}

std::size_t check_flagstick(const world_marker_batch& batch,
                            std::size_t index,
                            const glm::vec3& position,
                            const float pin_visual_height_meters) {
    const std::vector<glm::vec3> quad = expected_quad_positions();
    const float pin_height = std::max(0.1f, pin_visual_height_meters);
    const glm::vec3 pin_base = position + glm::vec3(0.0f, pin_height * 0.5f, 0.0f);
    const glm::vec3 pole_color(0.95f, 0.90f, 0.68f);
    const glm::vec3 flag_color(0.96f, 0.78f, 0.20f);
    index = check_piece(batch, index, quad, expected_panel_model(pin_base, 0.0f, glm::vec3(0.045f, pin_height * 0.5f, 1.0f)), pole_color);
    index = check_piece(batch, index, quad, expected_panel_model(pin_base, 90.0f, glm::vec3(0.045f, pin_height * 0.5f, 1.0f)), pole_color);
    const glm::vec3 flag_center = position + glm::vec3(0.34f, pin_height * 0.86f, 0.0f);
    index = check_piece(batch, index, quad, expected_panel_model(flag_center, 0.0f, glm::vec3(0.36f, 0.24f, 1.0f)), flag_color);
    index = check_piece(batch, index, quad, expected_panel_model(flag_center, 90.0f, glm::vec3(0.36f, 0.24f, 1.0f)), flag_color);
    return index;
}

glm::mat4 expected_cup_model(const glm::vec3& position, const float cup_radius) {
    const float cup_scale = cup_radius * 2.0f;
    return glm::scale(glm::translate(glm::mat4(1.0f), position + glm::vec3(0.0f, 0.09f, 0.0f)),
                      glm::vec3(cup_scale, 1.0f, cup_scale));
}

glm::mat4 expected_aim_dot_model(const glm::vec3& point, const std::size_t i) {
    const float scale = 0.35f + static_cast<float>(i % 3) * 0.04f;
    return glm::scale(glm::translate(glm::mat4(1.0f), point + glm::vec3(0.0f, 0.05f, 0.0f)),
                      glm::vec3(scale, 1.0f, scale));
}

std::vector<glm::vec3> make_arc(const std::size_t count) {
    std::vector<glm::vec3> points;
    for (std::size_t i = 0; i < count; ++i) {
        const float f = static_cast<float>(i);
        points.emplace_back(1.5f + f * 0.8f, 2.0f + std::sin(f) * 3.0f, -4.0f + f * 1.3f);
    }
    return points;
}
}

TEST_CASE("world marker unit disc and quad have the expected vertices") {
    const std::vector<glm::vec3> disc = make_unit_disc_positions(world_marker_disc_segments);
    const std::vector<glm::vec3> old = expected_disc_positions();
    REQUIRE(disc.size() == disc_vertex_count);
    REQUIRE(old.size() == disc_vertex_count);
    for (std::size_t i = 0; i < disc.size(); ++i) {
        CHECK(disc[i] == old[i]);
    }
    CHECK(make_unit_quad_positions() == expected_quad_positions());
    CHECK(make_unit_disc_positions(0).empty());
}

TEST_CASE("world marker batch builds hole markers, aim dots and club in order") {
    const std::vector<glm::vec3> arc = make_arc(28);

    world_marker_scene scene;
    scene.show_hole = true;
    scene.tee_position = glm::vec3(3.0f, 1.25f, -8.0f);
    scene.pin_position = glm::vec3(-12.0f, 4.5f, 140.0f);
    scene.cup_radius_meters = 0.65f;
    scene.pin_visual_height_meters = 2.1f;
    scene.show_aim_indicator = true;
    scene.aim_arc_points = &arc;
    scene.show_swing_club = true;
    scene.ball_position = glm::vec3(3.2f, 1.35f, -7.5f);
    scene.ball_visual_radius_meters = 0.1f;
    scene.aim_angle = 0.7f;
    scene.swing_power = 0.45f;

    world_marker_batch batch;
    build_world_marker_batch(batch, scene);

    const std::vector<glm::vec3> disc = expected_disc_positions();
    const std::vector<glm::vec3> quad = expected_quad_positions();

    std::size_t index = 0;
    index = check_piece(batch, index, disc, expected_ground_marker_model(scene.tee_position, 1.8f), glm::vec3(0.45f, 0.30f, 0.16f));
    const std::size_t cup_first = index;
    index = check_piece(batch, index, disc, expected_cup_model(scene.pin_position, 0.65f), glm::vec3(0.03f, 0.03f, 0.035f));
    const std::size_t flagstick_first = index;
    index = check_flagstick(batch, index, scene.pin_position, 2.1f);
    for (std::size_t i = 0; i < arc.size(); ++i) {
        index = check_piece(batch, index, disc, expected_aim_dot_model(arc[i], i), glm::vec3(0.95f, 0.78f, 0.22f));
    }
    for (const expected_panel& panel : expected_swing_club(scene.ball_position, 0.1f, 0.7f, 0.45f)) {
        index = check_piece(batch, index, quad, panel.model, panel.color);
    }
    CHECK(index == batch.vertices().size());

    // Tee (depth on) | cup (depth off) | flagstick + dots + club (depth on).
    REQUIRE(batch.runs().size() == 3U);
    check_run(batch.runs()[0], true, 0, disc_vertex_count);
    check_run(batch.runs()[1], false, cup_first, disc_vertex_count);
    check_run(batch.runs()[2],
              true,
              flagstick_first,
              flagstick_vertex_count + arc.size() * disc_vertex_count + club_vertex_count);
}

TEST_CASE("world marker pin cross pole and flag are the 90 degree yaw copies") {
    world_marker_batch batch;
    const glm::vec3 pin(10.0f, 2.0f, -5.0f);
    append_pin_flagstick(batch, pin, 2.0f);
    REQUIRE(batch.vertices().size() == flagstick_vertex_count);

    // Quad corner (1, 1, 0): the facing pole spreads along +x, the cross pole
    // along -z (yaw +90 maps +x to -z), both reaching the pin top.
    const glm::vec3 pole_corner = batch.vertices()[2].position;
    const glm::vec3 cross_corner = batch.vertices()[quad_vertex_count + 2].position;
    check_near(pole_corner, glm::vec3(10.045f, 4.0f, -5.0f));
    check_near(cross_corner, glm::vec3(10.0f, 4.0f, -5.045f));

    const glm::vec3 flag_corner = batch.vertices()[2 * quad_vertex_count + 2].position;
    const glm::vec3 flag_cross_corner = batch.vertices()[3 * quad_vertex_count + 2].position;
    check_near(flag_corner, glm::vec3(10.0f + 0.34f + 0.36f, 2.0f + 1.72f + 0.24f, -5.0f));
    check_near(flag_cross_corner, glm::vec3(10.34f, 2.0f + 1.72f + 0.24f, -5.36f));

    // A too-short pin is clamped to 0.1 m.
    world_marker_batch short_batch;
    append_pin_flagstick(short_batch, pin, 0.0f);
    check_flagstick(short_batch, 0, pin, 0.0f);
}

TEST_CASE("world marker batch groups a hub into three runs in submission order") {
    const std::vector<glm::vec3> starts = {
        glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(40.0f, 1.0f, 10.0f), glm::vec3(80.0f, 2.0f, -30.0f)};
    const std::vector<glm::vec3> tees = {
        glm::vec3(2.0f, 0.6f, 3.0f), glm::vec3(42.0f, 1.1f, 13.0f), glm::vec3(82.0f, 2.1f, -27.0f)};
    const std::vector<glm::vec3> pins = {
        glm::vec3(5.0f, 3.0f, 120.0f), glm::vec3(45.0f, -1.0f, 150.0f), glm::vec3(85.0f, 6.0f, 90.0f)};

    world_marker_scene scene;
    scene.show_hole = false;
    scene.start_markers = &starts;
    scene.tee_markers = &tees;
    scene.pin_markers = &pins;
    scene.cup_radius_meters = 0.75f;
    scene.pin_visual_height_meters = 2.4f;

    world_marker_batch batch;
    build_world_marker_batch(batch, scene);

    const std::vector<glm::vec3> disc = expected_disc_positions();
    std::size_t index = 0;
    for (const glm::vec3& start : starts) {
        index = check_piece(batch, index, disc, expected_ground_marker_model(start, 2.2f), glm::vec3(0.82f, 0.68f, 0.28f));
    }
    for (const glm::vec3& tee : tees) {
        index = check_piece(batch, index, disc, expected_ground_marker_model(tee, 1.45f), glm::vec3(0.45f, 0.30f, 0.16f));
    }
    const std::size_t cups_first = index;
    for (const glm::vec3& pin : pins) {
        index = check_piece(batch, index, disc, expected_cup_model(pin, 0.75f), glm::vec3(0.03f, 0.03f, 0.035f));
    }
    const std::size_t flagsticks_first = index;
    for (const glm::vec3& pin : pins) {
        index = check_flagstick(batch, index, pin, 2.4f);
    }
    CHECK(index == batch.vertices().size());

    REQUIRE(batch.runs().size() == 3U);
    check_run(batch.runs()[0], true, 0, 6 * disc_vertex_count);
    check_run(batch.runs()[1], false, cups_first, 3 * disc_vertex_count);
    check_run(batch.runs()[2], true, flagsticks_first, 3 * flagstick_vertex_count);
}

TEST_CASE("world marker batch keeps every cup ahead of its own flagstick") {
    const std::vector<glm::vec3> pins = {glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(9.0f, 0.0f, 9.0f)};

    world_marker_scene scene;
    scene.show_hole = true;
    scene.pin_position = glm::vec3(-20.0f, 0.0f, 50.0f);
    scene.pin_markers = &pins;

    world_marker_batch batch;
    build_world_marker_batch(batch, scene);

    // Primary tee | primary cup | primary flagstick | hub cups | hub flagsticks.
    REQUIRE(batch.runs().size() == 5U);
    const std::vector<bool> depth_writes = {true, false, true, false, true};
    for (std::size_t i = 0; i < depth_writes.size(); ++i) {
        CHECK(batch.runs()[i].depth_write == depth_writes[i]);
    }
    // Runs tile the vertex buffer with no gaps.
    std::size_t next = 0;
    for (const world_marker_run& run : batch.runs()) {
        CHECK(run.first == next);
        next += run.count;
    }
    CHECK(next == batch.vertices().size());
}

TEST_CASE("world marker batch skips hidden and missing pieces") {
    const std::vector<glm::vec3> arc = make_arc(5);

    world_marker_scene scene;
    scene.show_hole = false;
    scene.show_aim_indicator = false;
    scene.aim_arc_points = &arc;
    scene.show_swing_club = false;

    world_marker_batch batch;
    build_world_marker_batch(batch, scene);
    CHECK(batch.empty());
    CHECK(batch.runs().empty());

    // Null vectors are treated as empty; aim indicator with no points draws nothing.
    scene.show_aim_indicator = true;
    scene.aim_arc_points = nullptr;
    build_world_marker_batch(batch, scene);
    CHECK(batch.empty());
    CHECK(batch.runs().empty());
}

TEST_CASE("world marker batch reuse clears without shrinking capacity") {
    const std::vector<glm::vec3> long_arc = make_arc(28);
    const std::vector<glm::vec3> short_arc = make_arc(3);

    world_marker_scene scene;
    scene.show_hole = true;
    scene.show_aim_indicator = true;
    scene.aim_arc_points = &long_arc;

    world_marker_batch batch;
    build_world_marker_batch(batch, scene);
    const std::vector<world_marker_vertex> first_frame = batch.vertices();
    const std::size_t vertex_capacity = batch.vertices().capacity();
    const std::size_t run_capacity = batch.runs().capacity();
    const world_marker_vertex* storage = batch.vertices().data();

    scene.aim_arc_points = &short_arc;
    build_world_marker_batch(batch, scene);
    CHECK(batch.vertices().size() == (2 + short_arc.size()) * disc_vertex_count + flagstick_vertex_count);
    CHECK(batch.vertices().capacity() == vertex_capacity);
    CHECK(batch.runs().capacity() == run_capacity);
    CHECK(batch.vertices().data() == storage);

    // Rebuilding the same frame yields identical data in the same storage.
    scene.aim_arc_points = &long_arc;
    build_world_marker_batch(batch, scene);
    CHECK(batch.vertices().data() == storage);
    REQUIRE(batch.vertices().size() == first_frame.size());
    for (std::size_t i = 0; i < first_frame.size(); ++i) {
        CHECK(batch.vertices()[i].position == first_frame[i].position);
        CHECK(batch.vertices()[i].color == first_frame[i].color);
    }
}

TEST_CASE("world marker alpha and appends extend runs only on equal state") {
    world_marker_batch batch;
    batch.append_quad(glm::mat4(1.0f), glm::vec3(0.1f, 0.2f, 0.3f), 0.5f, true);
    batch.append_disc(glm::mat4(1.0f), glm::vec3(0.4f, 0.5f, 0.6f), 1.0f, true);
    batch.append_disc(glm::mat4(1.0f), glm::vec3(0.7f, 0.8f, 0.9f), 1.0f, false);
    batch.append_quad(glm::mat4(1.0f), glm::vec3(1.0f), 1.0f, true);

    CHECK(batch.vertices()[0].color == glm::vec4(0.1f, 0.2f, 0.3f, 0.5f));
    CHECK(batch.vertices()[quad_vertex_count].color == glm::vec4(0.4f, 0.5f, 0.6f, 1.0f));
    REQUIRE(batch.runs().size() == 3U);
    check_run(batch.runs()[0], true, 0, quad_vertex_count + disc_vertex_count);
    check_run(batch.runs()[1], false, quad_vertex_count + disc_vertex_count, disc_vertex_count);
    check_run(batch.runs()[2], true, quad_vertex_count + 2 * disc_vertex_count, quad_vertex_count);
}

// --- Golf cart ------------------------------------------------------------
//
// The cart is checked against an independent write-out of its pieces: unit
// cylinder and sphere generators, the camera-local frame, and every size,
// rotation and colour.

namespace {
constexpr std::size_t cart_cylinder_vertex_count = 96;   // 8 segments * 12
constexpr std::size_t cart_sphere_vertex_count = 576;    // 8 * 12 * 6

void expected_mesh_vertex(std::vector<float>& vertices, const glm::vec3 position, const glm::vec3 normal) {
    vertices.insert(vertices.end(), {
        position.x, position.y, position.z,
        normal.x, normal.y, normal.z
    });
}

// renderer.cpp's make_cylinder_vertices, before it moved to primitive_mesh.
std::vector<float> expected_cylinder_vertices(const int segments) {
    std::vector<float> vertices;
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

        expected_mesh_vertex(vertices, p00, n0);
        expected_mesh_vertex(vertices, p01, n1);
        expected_mesh_vertex(vertices, p11, n1);
        expected_mesh_vertex(vertices, p00, n0);
        expected_mesh_vertex(vertices, p11, n1);
        expected_mesh_vertex(vertices, p10, n0);

        expected_mesh_vertex(vertices, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
        expected_mesh_vertex(vertices, p01, glm::vec3(0.0f, -1.0f, 0.0f));
        expected_mesh_vertex(vertices, p00, glm::vec3(0.0f, -1.0f, 0.0f));

        expected_mesh_vertex(vertices, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        expected_mesh_vertex(vertices, p10, glm::vec3(0.0f, 1.0f, 0.0f));
        expected_mesh_vertex(vertices, p11, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    return vertices;
}

// renderer.cpp's make_sphere_vertices, before it moved to primitive_mesh.
std::vector<float> expected_sphere_vertices(const int latitude_segments, const int longitude_segments) {
    std::vector<float> vertices;
    constexpr float pi = 3.14159265358979323846f;
    const auto append = [&vertices](const glm::vec3 normal) {
        constexpr float radius = 1.0f;
        const glm::vec3 position = normal * radius;
        vertices.insert(vertices.end(), {
            position.x, position.y, position.z,
            normal.x, normal.y, normal.z
        });
    };

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

            append(p00);
            append(p10);
            append(p11);
            append(p00);
            append(p11);
            append(p01);
        }
    }
    return vertices;
}

std::vector<glm::vec3> positions_of(const std::vector<float>& interleaved) {
    std::vector<glm::vec3> positions;
    for (std::size_t i = 0; i + 5 < interleaved.size(); i += 6) {
        positions.emplace_back(interleaved[i], interleaved[i + 1], interleaved[i + 2]);
    }
    return positions;
}

// The camera-local frame (see renderer/camera_local.h), written out independently.
glm::mat4 expected_local_model(const glm::vec3& camera_position,
                          const glm::vec3& camera_target,
                          const glm::vec3& local,
                          const glm::vec3& rotation,
                          const glm::vec3& scale) {
    glm::vec3 forward = camera_target - camera_position;
    forward.y = 0.0f;
    forward = glm::normalize(glm::length(forward) > 0.0001f ? forward : glm::vec3(0.0f, 0.0f, 1.0f));
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

enum class cart_shape { quad, cylinder, sphere };

struct expected_cart_piece {
    cart_shape shape = cart_shape::quad;
    glm::mat4 model = glm::mat4(1.0f);
    glm::vec3 color = glm::vec3(1.0f);
};

// The cart as (shape, model, color), in draw order.
std::vector<expected_cart_piece> expected_cart_pieces(const glm::vec3& camera_position, const glm::vec3& camera_target) {
    const glm::vec3 body(0.36f, 0.46f, 0.20f);
    const glm::vec3 trim(0.08f, 0.09f, 0.08f);
    const glm::vec3 cream(0.76f, 0.72f, 0.56f);

    std::vector<expected_cart_piece> pieces;
    const auto panel = [&](const glm::vec3& local,
                           const glm::vec3& rotation,
                           const glm::vec2& half_size,
                           const glm::vec3& color) {
        // draw_local_panel
        pieces.push_back({cart_shape::quad,
                          expected_local_model(camera_position, camera_target, local, rotation, glm::vec3(half_size, 1.0f)),
                          color});
    };
    const auto cylinder = [&](const glm::vec3& local,
                              const glm::vec3& rotation,
                              const glm::vec3& scale,
                              const glm::vec3& color) {
        // draw_local_cylinder: basis, then scale, then translate half down.
        glm::mat4 model = expected_local_model(camera_position, camera_target, local, rotation, glm::vec3(1.0f));
        model = glm::scale(model, scale);
        model = glm::translate(model, glm::vec3(0.0f, -0.5f, 0.0f));
        pieces.push_back({cart_shape::cylinder, model, color});
    };
    const auto sphere = [&](const glm::vec3& local, const float radius, const glm::vec3& color) {
        // draw_local_sphere
        pieces.push_back({cart_shape::sphere,
                          expected_local_model(camera_position, camera_target, local, glm::vec3(0.0f), glm::vec3(radius)),
                          color});
    };

    panel(glm::vec3(0.0f, -0.79f, 1.08f), glm::vec3(glm::radians(78.0f), 0.0f, 0.0f), glm::vec2(0.74f, 0.48f), body);
    panel(glm::vec3(0.0f, -0.63f, 0.58f), glm::vec3(glm::radians(82.0f), 0.0f, 0.0f), glm::vec2(0.68f, 0.18f), trim);
    panel(glm::vec3(0.0f, -0.50f, 0.72f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec2(0.64f, 0.13f), cream);
    panel(glm::vec3(-0.78f, -0.63f, 0.86f), glm::vec3(0.0f, glm::radians(90.0f), 0.0f), glm::vec2(0.42f, 0.16f), body);
    panel(glm::vec3(0.78f, -0.63f, 0.86f), glm::vec3(0.0f, glm::radians(90.0f), 0.0f), glm::vec2(0.42f, 0.16f), body);
    panel(glm::vec3(0.0f, 0.23f, 0.72f), glm::vec3(glm::radians(88.0f), 0.0f, 0.0f), glm::vec2(0.84f, 0.42f), glm::vec3(0.72f, 0.68f, 0.47f));
    cylinder(glm::vec3(0.0f, -0.63f, 0.40f), glm::vec3(glm::radians(68.0f), 0.0f, glm::radians(90.0f)), glm::vec3(0.22f, 0.035f, 0.22f), glm::vec3(0.025f, 0.025f, 0.025f));
    cylinder(glm::vec3(0.0f, -0.73f, 0.48f), glm::vec3(glm::radians(22.0f), 0.0f, 0.0f), glm::vec3(0.024f, 0.30f, 0.024f), trim);

    const std::array<float, 2> post_x{-0.56f, 0.56f};
    const std::array<float, 2> post_z{0.46f, 1.10f};
    for (const float x : post_x) {
        for (const float z : post_z) {
            cylinder(glm::vec3(x, -0.17f, z), glm::vec3(0.0f), glm::vec3(0.035f, 0.84f, 0.035f), cream);
        }
    }

    const std::array<float, 2> wheel_x{-0.68f, 0.68f};
    const std::array<float, 1> wheel_z{1.30f};
    for (const float x : wheel_x) {
        for (const float z : wheel_z) {
            cylinder(glm::vec3(x, -1.08f, z), glm::vec3(0.0f, 0.0f, glm::radians(-90.0f)), glm::vec3(0.23f, 0.16f, 0.23f), glm::vec3(0.025f, 0.025f, 0.025f));
            sphere(glm::vec3(x, -1.08f, z), 0.085f, glm::vec3(0.58f, 0.56f, 0.48f));
        }
    }
    return pieces;
}

const std::vector<glm::vec3>& cart_shape_positions(const cart_shape shape) {
    static const std::vector<glm::vec3> quad = expected_quad_positions();
    static const std::vector<glm::vec3> cylinder = positions_of(expected_cylinder_vertices(8));
    static const std::vector<glm::vec3> sphere = positions_of(expected_sphere_vertices(8, 12));
    switch (shape) {
    case cart_shape::cylinder:
        return cylinder;
    case cart_shape::sphere:
        return sphere;
    case cart_shape::quad:
    default:
        return quad;
    }
}

const glm::vec3 test_camera_position(12.5f, 2.25f, -37.0f);
const glm::vec3 test_camera_target(14.0f, 1.80f, -20.0f);
}

TEST_CASE("cart unit primitives are the same data the renderer uploads to its VBOs") {
    // The batch's cylinder/sphere positions must be the position half of the
    // interleaved buffers renderer.cpp uploads — one generator, no fork.
    const std::vector<float> cylinder_interleaved = make_cylinder_vertices(primitive_cylinder_segments);
    const std::vector<glm::vec3> cylinder = make_cylinder_positions(primitive_cylinder_segments);
    REQUIRE(cylinder.size() == cart_cylinder_vertex_count);
    CHECK(cylinder_interleaved.size() == cart_cylinder_vertex_count * 6U);
    CHECK(cylinder == mesh_positions_of(cylinder_interleaved));

    const std::vector<float> sphere_interleaved = make_sphere_vertices(primitive_sphere_latitude_segments,
                                                                      primitive_sphere_longitude_segments);
    const std::vector<glm::vec3> sphere = make_sphere_positions(primitive_sphere_latitude_segments,
                                                               primitive_sphere_longitude_segments);
    REQUIRE(sphere.size() == cart_sphere_vertex_count);
    CHECK(sphere_interleaved.size() == cart_sphere_vertex_count * 6U);
    CHECK(sphere == mesh_positions_of(sphere_interleaved));

    // And the values match the independent generators above.
    CHECK(primitive_cylinder_segments == 8);
    CHECK(primitive_sphere_latitude_segments == 8);
    CHECK(primitive_sphere_longitude_segments == 12);
    CHECK(cylinder_interleaved == expected_cylinder_vertices(8));
    CHECK(sphere_interleaved == expected_sphere_vertices(8, 12));
}

TEST_CASE("world marker batch reproduces the golf cart's sixteen draws") {
    world_marker_scene scene;
    scene.show_hole = false;
    scene.cart_active = true;
    scene.camera_position = test_camera_position;
    scene.camera_target = test_camera_target;

    world_marker_batch batch;
    build_world_marker_batch(batch, scene);

    const std::vector<expected_cart_piece> pieces = expected_cart_pieces(test_camera_position, test_camera_target);
    REQUIRE(pieces.size() == 16U);
    CHECK(batch.vertices().size() == cart_model_vertex_count());
    CHECK(cart_model_vertex_count() == 6U * quad_vertex_count +
                                       8U * cart_cylinder_vertex_count +
                                       2U * cart_sphere_vertex_count);

    std::size_t index = 0;
    for (const expected_cart_piece& piece : pieces) {
        index = check_piece(batch, index, cart_shape_positions(piece.shape), piece.model, piece.color, 1.0f);
    }
    CHECK(index == batch.vertices().size());

    // One opaque, depth-writing run for the whole cart.
    REQUIRE(batch.runs().size() == 1U);
    check_run(batch.runs()[0], true, 0, batch.vertices().size());
}

TEST_CASE("golf cart is submitted ahead of the world markers in a depth-writing run") {
    const std::vector<glm::vec3> pins = {glm::vec3(4.0f, 0.0f, 12.0f)};

    world_marker_scene scene;
    scene.show_hole = true;
    scene.tee_position = glm::vec3(2.0f, 0.5f, -3.0f);
    scene.pin_position = glm::vec3(-6.0f, 0.25f, 44.0f);
    scene.pin_markers = &pins;
    scene.cart_active = true;
    scene.camera_position = test_camera_position;
    scene.camera_target = test_camera_target;

    world_marker_batch batch;
    build_world_marker_batch(batch, scene);

    const std::size_t cart_vertices = cart_model_vertex_count();
    // Cart first, then the hole tee disc, all in the leading depth-writing run.
    REQUIRE(batch.vertices().size() > cart_vertices);
    const std::vector<expected_cart_piece> pieces = expected_cart_pieces(test_camera_position, test_camera_target);
    std::size_t index = 0;
    for (const expected_cart_piece& piece : pieces) {
        index = check_piece(batch, index, cart_shape_positions(piece.shape), piece.model, piece.color, 1.0f);
    }
    CHECK(index == cart_vertices);
    check_piece(batch,
                cart_vertices,
                expected_disc_positions(),
                expected_ground_marker_model(scene.tee_position, 1.8f),
                glm::vec3(0.45f, 0.30f, 0.16f));

    // Cart + primary tee | primary cup | primary flagstick | hub cup | hub flagstick.
    REQUIRE(batch.runs().size() == 5U);
    const std::vector<bool> depth_writes = {true, false, true, false, true};
    for (std::size_t i = 0; i < depth_writes.size(); ++i) {
        CHECK(batch.runs()[i].depth_write == depth_writes[i]);
    }
    // The cart lives entirely inside the first run, which starts at vertex 0
    // and ends after the tee disc, so the depth-write-off cup run still comes
    // after the whole cart.
    CHECK(batch.runs()[0].first == 0U);
    CHECK(batch.runs()[0].count == cart_vertices + disc_vertex_count);
}

TEST_CASE("inactive golf cart appends nothing") {
    world_marker_scene scene;
    scene.show_hole = false;
    scene.cart_active = false;
    scene.camera_position = test_camera_position;
    scene.camera_target = test_camera_target;

    world_marker_batch batch;
    build_world_marker_batch(batch, scene);
    CHECK(batch.empty());
    CHECK(batch.runs().empty());

    // And an inactive cart leaves the marker layout untouched.
    scene.show_hole = true;
    build_world_marker_batch(batch, scene);
    const std::size_t markers_only = batch.vertices().size();
    CHECK(markers_only == 2U * disc_vertex_count + flagstick_vertex_count);

    scene.cart_active = true;
    build_world_marker_batch(batch, scene);
    CHECK(batch.vertices().size() == markers_only + cart_model_vertex_count());
}

TEST_CASE("another player is a figure on foot, the cart when driving, with a ring for their group") {
    world_marker_batch batch;
    render_remote_avatar avatar;
    avatar.position = glm::vec3(10.0f, 2.0f, -4.0f);
    avatar.player_id = 3;
    append_remote_avatar(batch, avatar, 1.65f);
    CHECK(batch.vertices().size() == remote_figure_vertex_count());
    // Standing on the ground, no taller than a person.
    float lowest = 1000.0f;
    float highest = -1000.0f;
    for (const world_marker_vertex& vertex : batch.vertices()) {
        lowest = std::min(lowest, vertex.position.y);
        highest = std::max(highest, vertex.position.y);
    }
    CHECK(lowest >= avatar.position.y - 0.05f);
    CHECK(highest <= avatar.position.y + 1.65f * 1.2f);

    batch.clear();
    avatar.in_cart = true;
    append_remote_avatar(batch, avatar, 1.65f);
    CHECK(batch.vertices().size() == cart_model_vertex_count());

    batch.clear();
    avatar.in_cart = false;
    avatar.group_id = 2;
    append_remote_avatar(batch, avatar, 1.65f);
    CHECK(batch.vertices().size() == remote_figure_vertex_count() + disc_vertex_count);
    CHECK(group_highlight(2) != group_highlight(3));
    CHECK(remote_tint(1) != remote_tint(2));
}

TEST_CASE("the marker batch draws the remote players and balls it is given") {
    world_marker_batch batch;
    const std::vector<render_remote_avatar> avatars(2);
    const std::vector<render_remote_ball> balls(3);
    world_marker_scene scene;
    scene.remote_avatars = &avatars;
    scene.remote_balls = &balls;
    scene.avatar_eye_height = 1.65f;
    scene.ball_visual_radius_meters = 0.05f;
    build_world_marker_batch(batch, scene);
    const std::size_t sphere = make_sphere_positions(primitive_sphere_latitude_segments, primitive_sphere_longitude_segments).size();
    CHECK(batch.vertices().size() == 2U * remote_figure_vertex_count() + 3U * sphere);
}
