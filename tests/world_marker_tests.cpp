#include "doctest.h"

#include "renderer/world_marker_batch.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// doctest-lite has no REQUIRE: record the failure, then leave the test so a
// size mismatch cannot index out of range.
#define WORLD_MARKER_REQUIRE(expr)     do {         CHECK(expr);         if (!(expr)) {             return;         }     } while (0)

namespace {
constexpr std::size_t disc_vertex_count = 54;
constexpr std::size_t quad_vertex_count = 6;
constexpr std::size_t flagstick_vertex_count = 4 * quad_vertex_count;
constexpr std::size_t club_vertex_count = 4 * quad_vertex_count;

// The renderer's old make_disc_vertices(18), as flat xyz floats.
std::vector<float> old_disc_vertices() {
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

std::vector<glm::vec3> old_disc_positions() {
    const std::vector<float> flat = old_disc_vertices();
    std::vector<glm::vec3> positions;
    for (std::size_t i = 0; i + 2 < flat.size(); i += 3) {
        positions.emplace_back(flat[i], flat[i + 1], flat[i + 2]);
    }
    return positions;
}

// The renderer's old screen_vao_ positions.
std::vector<glm::vec3> old_quad_positions() {
    return {
        glm::vec3(-1.0f, -1.0f, 0.0f),
        glm::vec3(1.0f, -1.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 0.0f),
        glm::vec3(-1.0f, -1.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 0.0f),
        glm::vec3(-1.0f, 1.0f, 0.0f),
    };
}

// Old per-draw model matrices, copied verbatim as the oracle.
glm::mat4 old_ground_marker_model(const glm::vec3& position, const float scale) {
    return glm::scale(glm::translate(glm::mat4(1.0f), position + glm::vec3(0.0f, 0.01f, 0.0f)),
                      glm::vec3(scale, 1.0f, scale));
}

glm::mat4 old_panel_model(const glm::vec3 center, const float yaw_degrees, const glm::vec3 scale) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
    model = glm::rotate(model, glm::radians(yaw_degrees), glm::vec3(0.0f, 1.0f, 0.0f));
    return glm::scale(model, scale);
}

float old_axis_x_yaw_radians(const glm::vec3& axis) {
    glm::vec3 flat(axis.x, 0.0f, axis.z);
    if (glm::length(flat) <= 0.0001f) {
        flat = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
        flat = glm::normalize(flat);
    }
    return std::atan2(-flat.z, flat.x);
}

glm::vec3 old_rotate_top_down_ccw_90_y(const glm::vec3& axis) {
    glm::vec3 rotated(-axis.z, 0.0f, axis.x);
    if (glm::length(rotated) <= 0.0001f) {
        return glm::vec3(1.0f, 0.0f, 0.0f);
    }
    return glm::normalize(rotated);
}

struct old_panel {
    glm::mat4 model;
    glm::vec3 color;
};

glm::mat4 old_world_panel_model(const glm::vec3& center,
                                const glm::vec3& axis_x,
                                const float local_z_rotation,
                                const glm::vec2& half_size) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
    model = glm::rotate(model, old_axis_x_yaw_radians(axis_x), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, local_z_rotation, glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::scale(model, glm::vec3(half_size, 1.0f));
}

// Old draw_swing_club, reduced to the (model, color) pairs it drew.
std::vector<old_panel> old_swing_club(const glm::vec3& ball_position,
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

    const glm::vec3 club_swing_side = -old_rotate_top_down_ccw_90_y(player_side);
    const glm::vec3 club_head_axis = old_rotate_top_down_ccw_90_y(forward);
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
        {old_world_panel_model(shaft_center, club_swing_side, swing_angle, glm::vec2(0.018f, shaft_length * 0.5f)),
         glm::vec3(0.82f, 0.78f, 0.62f)},
        {old_world_panel_model(head_center + club_head_axis * 0.03f, club_face_axis, 0.0f, glm::vec2(0.16f, 0.040f)),
         glm::vec3(0.16f, 0.15f, 0.13f)},
        {old_world_panel_model(head_center + club_head_axis * 0.055f, -club_face_axis, 0.0f, glm::vec2(0.14f, 0.045f)),
         glm::vec3(0.20f, 0.19f, 0.17f)},
        {old_world_panel_model(head_center - club_swing_side * 0.02f, club_face_axis, 0.0f, glm::vec2(0.13f, 0.035f)),
         glm::vec3(0.09f, 0.085f, 0.075f)},
    };
}

void check_near(const glm::vec3& actual, const glm::vec3& expected) {
    CHECK(std::abs(actual.x - expected.x) <= 1e-5f);
    CHECK(std::abs(actual.y - expected.y) <= 1e-5f);
    CHECK(std::abs(actual.z - expected.z) <= 1e-5f);
}

// Checks that vertices [first, first + local.size()) are old_model * local
// with the given flat color and alpha. Returns the index after the piece.
std::size_t check_piece(const world_marker_batch& batch,
                        const std::size_t first,
                        const std::vector<glm::vec3>& local,
                        const glm::mat4& old_model,
                        const glm::vec3& color,
                        const float alpha = 1.0f) {
    const std::vector<world_marker_vertex>& vertices = batch.vertices();
    CHECK(first + local.size() <= vertices.size());
    if (first + local.size() > vertices.size()) {
        return first + local.size();
    }
    for (std::size_t i = 0; i < local.size(); ++i) {
        const glm::vec4 expected = old_model * glm::vec4(local[i], 1.0f);
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
    const std::vector<glm::vec3> quad = old_quad_positions();
    const float pin_height = std::max(0.1f, pin_visual_height_meters);
    const glm::vec3 pin_base = position + glm::vec3(0.0f, pin_height * 0.5f, 0.0f);
    const glm::vec3 pole_color(0.95f, 0.90f, 0.68f);
    const glm::vec3 flag_color(0.96f, 0.78f, 0.20f);
    index = check_piece(batch, index, quad, old_panel_model(pin_base, 0.0f, glm::vec3(0.045f, pin_height * 0.5f, 1.0f)), pole_color);
    index = check_piece(batch, index, quad, old_panel_model(pin_base, 90.0f, glm::vec3(0.045f, pin_height * 0.5f, 1.0f)), pole_color);
    const glm::vec3 flag_center = position + glm::vec3(0.34f, pin_height * 0.86f, 0.0f);
    index = check_piece(batch, index, quad, old_panel_model(flag_center, 0.0f, glm::vec3(0.36f, 0.24f, 1.0f)), flag_color);
    index = check_piece(batch, index, quad, old_panel_model(flag_center, 90.0f, glm::vec3(0.36f, 0.24f, 1.0f)), flag_color);
    return index;
}

glm::mat4 old_cup_model(const glm::vec3& position, const float cup_radius, const float cup_visual_radius_meters) {
    const float cup_scale = std::max(cup_visual_radius_meters * 2.0f, cup_radius * 2.0f);
    return glm::scale(glm::translate(glm::mat4(1.0f), position + glm::vec3(0.0f, 0.09f, 0.0f)),
                      glm::vec3(cup_scale, 1.0f, cup_scale));
}

glm::mat4 old_aim_dot_model(const glm::vec3& point, const std::size_t i) {
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

TEST_CASE("world marker unit disc matches the old marker vertex buffer") {
    const std::vector<glm::vec3> disc = make_unit_disc_positions(world_marker_disc_segments);
    const std::vector<glm::vec3> old = old_disc_positions();
    WORLD_MARKER_REQUIRE(disc.size() == disc_vertex_count);
    WORLD_MARKER_REQUIRE(old.size() == disc_vertex_count);
    for (std::size_t i = 0; i < disc.size(); ++i) {
        CHECK(disc[i] == old[i]);
    }
    CHECK(make_unit_quad_positions() == old_quad_positions());
    CHECK(make_unit_disc_positions(0).empty());
}

TEST_CASE("world marker batch reproduces single-hole markers, aim dots and club") {
    const std::vector<glm::vec3> arc = make_arc(28);

    world_marker_scene scene;
    scene.show_primary_hole_markers = true;
    scene.tee_position = glm::vec3(3.0f, 1.25f, -8.0f);
    scene.pin_position = glm::vec3(-12.0f, 4.5f, 140.0f);
    scene.cup_radius = 0.65f;
    scene.cup_visual_radius_meters = 0.4f;  // cup_radius wins the max
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

    const std::vector<glm::vec3> disc = old_disc_positions();
    const std::vector<glm::vec3> quad = old_quad_positions();

    std::size_t index = 0;
    index = check_piece(batch, index, disc, old_ground_marker_model(scene.tee_position, 1.8f), glm::vec3(0.45f, 0.30f, 0.16f));
    const std::size_t cup_first = index;
    index = check_piece(batch, index, disc, old_cup_model(scene.pin_position, 0.65f, 0.4f), glm::vec3(0.03f, 0.03f, 0.035f));
    const std::size_t flagstick_first = index;
    index = check_flagstick(batch, index, scene.pin_position, 2.1f);
    for (std::size_t i = 0; i < arc.size(); ++i) {
        index = check_piece(batch, index, disc, old_aim_dot_model(arc[i], i), glm::vec3(0.95f, 0.78f, 0.22f));
    }
    for (const old_panel& panel : old_swing_club(scene.ball_position, 0.1f, 0.7f, 0.45f)) {
        index = check_piece(batch, index, quad, panel.model, panel.color);
    }
    CHECK(index == batch.vertices().size());

    // Tee (depth on) | cup (depth off) | flagstick + dots + club (depth on).
    WORLD_MARKER_REQUIRE(batch.runs().size() == 3U);
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
    WORLD_MARKER_REQUIRE(batch.vertices().size() == flagstick_vertex_count);

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

    // A too-short pin is clamped to 0.1 m exactly like the old code.
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
    scene.show_primary_hole_markers = false;
    scene.start_markers = &starts;
    scene.tee_markers = &tees;
    scene.pin_markers = &pins;
    scene.cup_radius = 0.65f;
    scene.cup_visual_radius_meters = 0.75f;  // visual radius wins the max
    scene.pin_visual_height_meters = 2.4f;

    world_marker_batch batch;
    build_world_marker_batch(batch, scene);

    const std::vector<glm::vec3> disc = old_disc_positions();
    std::size_t index = 0;
    for (const glm::vec3& start : starts) {
        index = check_piece(batch, index, disc, old_ground_marker_model(start, 2.2f), glm::vec3(0.82f, 0.68f, 0.28f));
    }
    for (const glm::vec3& tee : tees) {
        index = check_piece(batch, index, disc, old_ground_marker_model(tee, 1.45f), glm::vec3(0.45f, 0.30f, 0.16f));
    }
    const std::size_t cups_first = index;
    for (const glm::vec3& pin : pins) {
        index = check_piece(batch, index, disc, old_cup_model(pin, 0.65f, 0.75f), glm::vec3(0.03f, 0.03f, 0.035f));
    }
    const std::size_t flagsticks_first = index;
    for (const glm::vec3& pin : pins) {
        index = check_flagstick(batch, index, pin, 2.4f);
    }
    CHECK(index == batch.vertices().size());

    WORLD_MARKER_REQUIRE(batch.runs().size() == 3U);
    check_run(batch.runs()[0], true, 0, 6 * disc_vertex_count);
    check_run(batch.runs()[1], false, cups_first, 3 * disc_vertex_count);
    check_run(batch.runs()[2], true, flagsticks_first, 3 * flagstick_vertex_count);
}

TEST_CASE("world marker batch keeps every cup ahead of its own flagstick") {
    const std::vector<glm::vec3> pins = {glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(9.0f, 0.0f, 9.0f)};

    world_marker_scene scene;
    scene.show_primary_hole_markers = true;
    scene.pin_position = glm::vec3(-20.0f, 0.0f, 50.0f);
    scene.pin_markers = &pins;

    world_marker_batch batch;
    build_world_marker_batch(batch, scene);

    // Primary tee | primary cup | primary flagstick | hub cups | hub flagsticks.
    WORLD_MARKER_REQUIRE(batch.runs().size() == 5U);
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
    scene.show_primary_hole_markers = false;
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
    scene.show_primary_hole_markers = true;
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
    WORLD_MARKER_REQUIRE(batch.vertices().size() == first_frame.size());
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
    WORLD_MARKER_REQUIRE(batch.runs().size() == 3U);
    check_run(batch.runs()[0], true, 0, quad_vertex_count + disc_vertex_count);
    check_run(batch.runs()[1], false, quad_vertex_count + disc_vertex_count, disc_vertex_count);
    check_run(batch.runs()[2], true, quad_vertex_count + 2 * disc_vertex_count, quad_vertex_count);
}

TEST_CASE("world marker GPU buffer capacity only grows") {
    CHECK(grow_buffer_capacity(0, 0) == 0U);
    CHECK(grow_buffer_capacity(0, 100) == 100U);
    CHECK(grow_buffer_capacity(1000, 999) == 1000U);
    CHECK(grow_buffer_capacity(1000, 1000) == 1000U);
    CHECK(grow_buffer_capacity(1000, 1001) == 2000U);
    CHECK(grow_buffer_capacity(1000, 5000) == 5000U);
    CHECK(grow_buffer_capacity(1000, 10) == 1000U);
}
