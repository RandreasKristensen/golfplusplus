#include "doctest.h"

#include "physics/vector_math.h"
#include "renderer/camera_local.h"
#include "renderer/cart_batch.h"
#include "renderer/held_props.h"
#include "renderer/primitive_mesh.h"
#include "renderer/prop_batch.h"
#include "renderer/remote_avatar_batch.h"
#include "renderer/swing_club.h"
#include "renderer/world_marker_batch.h"
#include "test_support.h"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <vector>

namespace {
constexpr std::size_t disc_vertex_count = 54;
constexpr std::size_t quad_vertex_count = 6;
constexpr std::size_t flagstick_vertex_count = 4 * quad_vertex_count;

// An 18-segment disc fan, radius 0.5, as flat xyz floats.
std::vector<float> expected_disc_vertices() {
    std::vector<float> vertices;
    constexpr int segments = 18;
    constexpr float radius = 0.5f;
    const float pi = glm::pi<float>();
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

void check_near(const glm::vec3& actual, const glm::vec3& expected) {
    CHECK(std::abs(actual.x - expected.x) <= 1e-5f);
    CHECK(std::abs(actual.y - expected.y) <= 1e-5f);
    CHECK(std::abs(actual.z - expected.z) <= 1e-5f);
}

// Checks that vertices [first, first + local.size()) are expected_model * local
// with the given flat color and glow. Returns the index after the piece.
std::size_t check_piece(const world_marker_batch& batch,
                        const std::size_t first,
                        const std::vector<glm::vec3>& local,
                        const glm::mat4& expected_model,
                        const glm::vec3& color,
                        const float glow = 0.0f) {
    const std::vector<world_marker_vertex>& vertices = batch.vertices();
    CHECK(first + local.size() <= vertices.size());
    if (first + local.size() > vertices.size()) {
        return first + local.size();
    }
    for (std::size_t i = 0; i < local.size(); ++i) {
        const glm::vec4 expected = expected_model * glm::vec4(local[i], 1.0f);
        check_near(vertices[first + i].position, glm::vec3(expected));
        CHECK(vertices[first + i].color == color);
        CHECK(vertices[first + i].glow == glow);
    }
    return first + local.size();
}

// A 12 x 8 m tee box topped at `center`, over ground half a metre lower.
tee_box make_tee_box(const glm::vec3& center, const glm::vec3& down_hole) {
    tee_box box;
    box.center = center;
    box.down_hole = down_hole;
    box.half_width = 6.0f;
    box.half_length = 4.0f;
    box.bottom = center.y - 0.5f;
    return box;
}

std::vector<world_marker_vertex> tee_box_vertices(const tee_box& box) {
    world_marker_batch batch;
    append_tee_box(batch, box, test_scene_colors().markers);
    return batch.vertices();
}

std::size_t tee_box_vertex_count() {
    return tee_box_vertices(make_tee_box(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f))).size();
}

// Checks that the vertices from `first` are append_tee_box's. Returns the
// index after the box.
std::size_t check_tee_box(const world_marker_batch& batch, const std::size_t first, const tee_box& box) {
    const std::vector<world_marker_vertex> expected = tee_box_vertices(box);
    const std::vector<world_marker_vertex>& vertices = batch.vertices();
    CHECK(first + expected.size() <= vertices.size());
    for (std::size_t i = 0; i < expected.size() && first + i < vertices.size(); ++i) {
        CHECK(vertices[first + i].position == expected[i].position);
        CHECK(vertices[first + i].color == expected[i].color);
    }
    return first + expected.size();
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
    const glm::vec3 pole_color = test_scene_colors().markers.pin_pole;
    const glm::vec3 flag_color = test_scene_colors().markers.flag;
    const glow_floors& glow = test_scene_colors().glow;
    index = check_piece(batch, index, quad, expected_panel_model(pin_base, 0.0f, glm::vec3(0.028f, pin_height * 0.5f, 1.0f)), pole_color,
                        glow.pin_pole);
    index = check_piece(batch, index, quad, expected_panel_model(pin_base, 90.0f, glm::vec3(0.028f, pin_height * 0.5f, 1.0f)), pole_color,
                        glow.pin_pole);
    // In calm the flag hangs from just under the pole's top, its fly end
    // 0.72 m out along +x and 0.35 rad from straight down.
    const glm::vec3 hoist_top = position + glm::vec3(0.0f, pin_height * 0.86f + 0.24f, 0.0f);
    const glm::vec3 hoist_bottom = hoist_top - glm::vec3(0.0f, 0.48f, 0.0f);
    const glm::vec3 fly = glm::vec3(std::sin(0.35f), -std::cos(0.35f), 0.0f) * 0.72f;
    const std::vector<glm::vec3> flag = {hoist_top,         hoist_bottom,           hoist_bottom + fly * 0.5f,
                                         hoist_top,         hoist_bottom + fly * 0.5f, hoist_top + fly * 0.5f,
                                         hoist_top + fly * 0.5f, hoist_bottom + fly * 0.5f, hoist_bottom + fly,
                                         hoist_top + fly * 0.5f, hoist_bottom + fly,  hoist_top + fly};
    for (const glm::vec3& corner : flag) {
        if (index >= batch.vertices().size()) {
            CHECK(index < batch.vertices().size());
            return index;
        }
        check_near(batch.vertices()[index].position, corner);
        CHECK(batch.vertices()[index].color == flag_color);
        CHECK(batch.vertices()[index].glow == glow.flag);
        ++index;
    }
    return index;
}

glm::mat4 expected_cup_model(const glm::vec3& position, const float cup_radius) {
    const float cup_scale = cup_radius * 2.0f;
    return glm::scale(glm::translate(glm::mat4(1.0f), position + glm::vec3(0.0f, 0.09f, 0.0f)),
                      glm::vec3(cup_scale, 1.0f, cup_scale));
}

std::vector<glm::vec3> make_points(const std::size_t count) {
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
    const std::vector<glm::vec3> expected = expected_disc_positions();
    REQUIRE(disc.size() == disc_vertex_count);
    REQUIRE(expected.size() == disc_vertex_count);
    for (std::size_t i = 0; i < disc.size(); ++i) {
        CHECK(disc[i] == expected[i]);
    }
    CHECK(make_unit_quad_positions() == expected_quad_positions());
    CHECK(make_unit_disc_positions(0).empty());
}

TEST_CASE("at address the club head rests on the ground behind the ball, its face looking along the aim") {
    render_club_pose pose;
    pose.ball_position = glm::vec3(3.0f, 1.5f, -2.0f);
    pose.aim_angle = 0.7f;
    constexpr float radius = 0.1f;
    const glm::vec3 forward = yaw_direction(pose.aim_angle);
    const prop_model& club = test_props().at("club_iron");
    prop_batch batch;
    append_swing_club(batch, pose, radius, club, test_scene_colors().glow);
    REQUIRE(batch.opaque().size() == prop_model_vertex_count(club));
    float lowest = batch.opaque().front().position.y;
    for (const prop_vertex& vertex : batch.opaque()) {
        lowest = std::min(lowest, vertex.position.y);
    }
    CHECK(std::abs(lowest - (pose.ball_position.y - radius)) <= 0.005f);
    // The face looks along the aim from just behind the ball, the grip held
    // up high on the golfer's side (a right-hander's left of the line).
    const glm::mat4 frame = swing_club_frame(pose, radius, club);
    const glm::vec3 face_down(frame * glm::vec4(club.point("face"), 1.0f));
    CHECK(glm::dot(glm::normalize(glm::vec3(frame[2])), forward) > 0.97f);
    CHECK(glm::dot(face_down - pose.ball_position, forward) < 0.0f);
    CHECK(horizontal_distance(face_down, pose.ball_position) < radius + 0.03f);
    const glm::vec3 grip(frame * glm::vec4(club.point("grip"), 1.0f));
    CHECK(near(grip, swing_club_grip(pose, radius), 0.0001f));
    CHECK(glm::dot(grip - pose.ball_position, yaw_left(forward)) > 0.0f);
    // Swung back, the head rises off the ground and goes back from the target.
    render_club_pose swung = pose;
    swung.raise = 1.0f;
    const glm::vec3 face_up(swing_club_frame(swung, radius, club) * glm::vec4(club.point("face"), 1.0f));
    CHECK(face_up.y > face_down.y + 0.3f);
    CHECK(glm::dot(face_up - face_down, forward) < 0.0f);
}

TEST_CASE("a putt is a short pendulum: the head goes back low, never swung up like a full swing") {
    render_club_pose pose;
    pose.ball_position = glm::vec3(0.0f, 0.0f, 0.0f);
    pose.putt = true;
    constexpr float radius = 0.1f;
    const glm::vec3 forward = yaw_direction(pose.aim_angle);
    const prop_model& club = test_props().at("club_putter");
    const glm::vec3 face_down(swing_club_frame(pose, radius, club) * glm::vec4(club.point("face"), 1.0f));
    render_club_pose back = pose;
    back.raise = 1.0f;
    const glm::vec3 face_back(swing_club_frame(back, radius, club) * glm::vec4(club.point("face"), 1.0f));
    const float distance_back = -glm::dot(face_back - face_down, forward);
    CHECK(distance_back > 0.3f);
    CHECK(distance_back < 0.6f);
    CHECK(face_back.y - face_down.y < 0.15f);
    // The grip moves with the shoulders, not hinged past them.
    const glm::vec3 grip_down = swing_club_grip(pose, radius);
    const glm::vec3 grip_back = swing_club_grip(back, radius);
    CHECK(glm::length(grip_back - grip_down) < distance_back * 0.5f);
}

TEST_CASE("the club's face turns with the setup, its shaft leans with the ball back, and a left-hander's is mirrored") {
    render_club_pose pose;
    pose.ball_position = glm::vec3(0.0f, 0.05f, 0.0f);
    constexpr float radius = 0.05f;
    const prop_model& club = test_props().at("club_iron");
    const auto face_of = [&](const render_club_pose& p) {
        return glm::normalize(horizontal(glm::vec3(swing_club_frame(p, radius, club)[2])));
    };
    const auto toe_of = [&](const render_club_pose& p) {
        const glm::mat4 frame = swing_club_frame(p, radius, club);
        return glm::vec3(frame * glm::vec4(club.point("face"), 1.0f)) - glm::vec3(frame * glm::vec4(club.point("grip"), 1.0f));
    };
    // Opened 20 degrees right of the path (a right-hander's open face).
    render_club_pose open = pose;
    open.face_turn = glm::radians(-20.0f);
    CHECK(face_of(open).x < -0.2f);
    CHECK(near(std::acos(glm::dot(face_of(open), face_of(pose))), glm::radians(20.0f), glm::radians(3.0f)));
    // Ball back: the hands ahead of the head, towards the target.
    render_club_pose leaning = pose;
    leaning.shaft_lean = glm::radians(10.0f);
    const glm::vec3 grip = swing_club_grip(leaning, radius);
    const glm::vec3 head(swing_club_frame(leaning, radius, club) * glm::vec4(club.point("face"), 1.0f));
    CHECK(grip.z > head.z + 0.1f);
    CHECK(swing_club_grip(pose, radius).z < grip.z);
    // A left-hander's grip is on the line's other side, the face still at the target.
    render_club_pose lefty = pose;
    lefty.left_handed = true;
    CHECK(swing_club_grip(lefty, radius).x < 0.0f);
    CHECK(swing_club_grip(pose, radius).x > 0.0f);
    CHECK(face_of(lefty).z > 0.97f);
    CHECK(toe_of(lefty).x * toe_of(pose).x < 0.0f);
}

TEST_CASE("world marker batch builds the hole's cup, then its flagstick") {
    world_marker_scene scene;
    scene.show_hole = true;
    scene.pin_position = glm::vec3(-12.0f, 4.5f, 140.0f);
    scene.cup_radius_meters = 0.65f;
    scene.pin_visual_height_meters = 2.1f;

    world_marker_batch batch;
    build_world_marker_batch(batch, scene, test_scene_colors());

    const std::vector<glm::vec3> disc = expected_disc_positions();
    std::size_t index = 0;
    const std::size_t cup_first = index;
    index = check_piece(batch, index, disc, expected_cup_model(scene.pin_position, 0.65f), test_scene_colors().markers.cup,
                        test_scene_colors().glow.cup);
    const std::size_t flagstick_first = index;
    index = check_flagstick(batch, index, scene.pin_position, 2.1f);
    CHECK(index == batch.vertices().size());

    // Cup (depth off) | flagstick (depth on).
    REQUIRE(batch.runs().size() == 2U);
    check_run(batch.runs()[0], false, cup_first, disc_vertex_count);
    check_run(batch.runs()[1], true, flagstick_first, flagstick_vertex_count);
}

TEST_CASE("a tee box is a flat top of grey tiles under a green mat, its sides down to the ground") {
    const glm::vec3 down_hole(0.6f, 0.0f, 0.8f);
    const glm::vec3 across(-0.8f, 0.0f, 0.6f);
    const tee_box box = make_tee_box(glm::vec3(10.0f, 2.0f, -5.0f), down_hole);
    const std::vector<world_marker_vertex> vertices = tee_box_vertices(box);
    // Top, four sides, 8 x 5 whole tiles of about 1.5 m, the mat.
    REQUIRE(vertices.size() == (5U + 40U + 1U) * quad_vertex_count);

    float lowest = box.center.y;
    for (const world_marker_vertex& vertex : vertices) {
        const glm::vec3 offset = vertex.position - box.center;
        // Never past the box's footprint.
        CHECK(std::abs(glm::dot(offset, down_hole)) <= box.half_length + 1e-4f);
        CHECK(std::abs(glm::dot(offset, across)) <= box.half_width + 1e-4f);
        lowest = std::min(lowest, vertex.position.y);
        const glm::vec3 color = vertex.color;
        const bool grey = std::abs(color.r - color.g) < 0.05f && std::abs(color.g - color.b) < 0.05f;
        const bool green = color.g > color.r && color.g > color.b;
        CHECK((grey || green));
    }
    // The sides reach below the lowest ground under the box.
    CHECK(lowest < box.bottom);

    // The top and the tiles are flat at the box's height; the mat is the last
    // quad, green, just over them.
    const std::size_t sides_end = 5U * quad_vertex_count;
    const std::size_t mat_first = vertices.size() - quad_vertex_count;
    for (std::size_t i = 0; i < quad_vertex_count; ++i) {
        CHECK(std::abs(vertices[i].position.y - box.center.y) < 1e-5f);
    }
    for (std::size_t i = sides_end; i < mat_first; ++i) {
        CHECK(std::abs(vertices[i].position.y - box.center.y) < 0.015f);
        CHECK(vertices[i].position.y < vertices[mat_first].position.y);
    }
    for (std::size_t i = mat_first; i < vertices.size(); ++i) {
        const glm::vec3 color = vertices[i].color;
        CHECK((color.g > color.r && color.g > color.b));
    }
}

TEST_CASE("world marker pin cross pole is the 90 degree yaw copy, the flag hung from its top") {
    world_marker_batch batch;
    const glm::vec3 pin(10.0f, 2.0f, -5.0f);
    append_pin_flagstick(batch, pin, 2.0f, test_scene_colors().markers, test_scene_colors().glow);
    REQUIRE(batch.vertices().size() == flagstick_vertex_count);

    // Quad corner (1, 1, 0): the facing pole spreads along +x, the cross pole
    // along -z (yaw +90 maps +x to -z), both reaching the pin top.
    const glm::vec3 pole_corner = batch.vertices()[2].position;
    const glm::vec3 cross_corner = batch.vertices()[quad_vertex_count + 2].position;
    check_near(pole_corner, glm::vec3(10.028f, 4.0f, -5.0f));
    check_near(cross_corner, glm::vec3(10.0f, 4.0f, -5.028f));
    check_flagstick(batch, 0, pin, 2.0f);

    // A too-short pin is clamped to 0.1 m.
    world_marker_batch short_batch;
    append_pin_flagstick(short_batch, pin, 0.0f, test_scene_colors().markers, test_scene_colors().glow);
    check_flagstick(short_batch, 0, pin, 0.0f);
}

TEST_CASE("a flag streams downwind, straight out in a strong wind and waving across it") {
    const glm::vec3 pin(0.0f, 0.0f, 0.0f);
    const auto fly_end = [&pin](const flag_pose& pose) {
        world_marker_batch batch;
        append_pin_flagstick(batch, pin, 2.0f, test_scene_colors().markers, test_scene_colors().glow, pose);
        CHECK(batch.vertices().size() == flagstick_vertex_count);
        // The fly end's top corner, less the hoist's.
        return batch.vertices().back().position - batch.vertices()[2 * quad_vertex_count].position;
    };
    const glm::vec3 south(0.0f, 0.0f, 1.0f);
    const glm::vec3 limp = fly_end(flag_pose{south * 0.5f, 0.0f, 0.0f, 0.0f});
    const glm::vec3 out = fly_end(flag_pose{south * 12.0f, 1.0f, 0.0f, 0.0f});
    CHECK(limp.z > 0.0f);
    CHECK(limp.y < -0.6f);
    check_near(out, glm::vec3(0.0f, 0.0f, 0.72f));
    // Its waves move the fly end across the wind, never along it.
    const glm::vec3 waving = fly_end(flag_pose{south * 12.0f, 1.0f, 1.0f, 0.1f});
    CHECK(std::abs(waving.x) > 0.01f);
    CHECK(near(waving.z, 0.72f));
}

TEST_CASE("the tee box batch holds every tee box in order, one run") {
    const std::vector<tee_box> boxes = {make_tee_box(glm::vec3(2.0f, 0.6f, 3.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
                                        make_tee_box(glm::vec3(42.0f, 1.1f, 13.0f), glm::vec3(0.6f, 0.0f, 0.8f))};
    world_marker_batch batch;
    build_tee_box_batch(batch, &boxes, test_scene_colors().markers);
    std::size_t index = 0;
    for (const tee_box& box : boxes) {
        index = check_tee_box(batch, index, box);
    }
    CHECK(index == batch.vertices().size());
    REQUIRE(batch.runs().size() == 1U);
    check_run(batch.runs()[0], true, 0, 2 * tee_box_vertex_count());

    build_tee_box_batch(batch, nullptr, test_scene_colors().markers);
    CHECK(batch.empty());
}

TEST_CASE("world marker batch groups a hub into two runs in submission order") {
    const std::vector<glm::vec3> pins = {
        glm::vec3(5.0f, 3.0f, 120.0f), glm::vec3(45.0f, -1.0f, 150.0f), glm::vec3(85.0f, 6.0f, 90.0f)};

    world_marker_scene scene;
    scene.show_hole = false;
    scene.pin_markers = &pins;
    scene.cup_radius_meters = 0.75f;
    scene.pin_visual_height_meters = 2.4f;

    world_marker_batch batch;
    build_world_marker_batch(batch, scene, test_scene_colors());

    const std::vector<glm::vec3> disc = expected_disc_positions();
    std::size_t index = 0;
    const std::size_t cups_first = index;
    for (const glm::vec3& pin : pins) {
        index = check_piece(batch, index, disc, expected_cup_model(pin, 0.75f), test_scene_colors().markers.cup,
                            test_scene_colors().glow.cup);
    }
    const std::size_t flagsticks_first = index;
    for (const glm::vec3& pin : pins) {
        index = check_flagstick(batch, index, pin, 2.4f);
    }
    CHECK(index == batch.vertices().size());

    REQUIRE(batch.runs().size() == 2U);
    check_run(batch.runs()[0], false, cups_first, 3 * disc_vertex_count);
    check_run(batch.runs()[1], true, flagsticks_first, 3 * flagstick_vertex_count);
}

TEST_CASE("world marker batch keeps every cup ahead of its own flagstick") {
    const std::vector<glm::vec3> pins = {glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(9.0f, 0.0f, 9.0f)};

    world_marker_scene scene;
    scene.show_hole = true;
    scene.pin_position = glm::vec3(-20.0f, 0.0f, 50.0f);
    scene.pin_markers = &pins;

    world_marker_batch batch;
    build_world_marker_batch(batch, scene, test_scene_colors());

    // Primary cup | primary flagstick | hub cups | hub flagsticks.
    REQUIRE(batch.runs().size() == 4U);
    const std::vector<bool> depth_writes = {false, true, false, true};
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
    world_marker_scene scene;
    scene.show_hole = false;
    world_marker_batch batch;
    build_world_marker_batch(batch, scene, test_scene_colors());
    CHECK(batch.empty());
    CHECK(batch.runs().empty());

    // Null vectors are treated as empty.
    scene.collectible_markers = nullptr;
    scene.pin_markers = nullptr;
    build_world_marker_batch(batch, scene, test_scene_colors());
    CHECK(batch.empty());
}

TEST_CASE("world marker batch reuse clears without shrinking capacity") {
    const std::vector<glm::vec3> long_arc = make_points(28);
    const std::vector<glm::vec3> short_arc = make_points(3);

    world_marker_scene scene;
    scene.show_hole = true;
    scene.collectible_markers = &long_arc;

    world_marker_batch batch;
    build_world_marker_batch(batch, scene, test_scene_colors());
    const std::vector<world_marker_vertex> first_frame = batch.vertices();
    const std::size_t vertex_capacity = batch.vertices().capacity();
    const std::size_t run_capacity = batch.runs().capacity();
    const world_marker_vertex* storage = batch.vertices().data();

    scene.collectible_markers = &short_arc;
    build_world_marker_batch(batch, scene, test_scene_colors());
    CHECK(batch.vertices().size() ==
          (1 + short_arc.size()) * disc_vertex_count + flagstick_vertex_count);
    CHECK(batch.vertices().capacity() == vertex_capacity);
    CHECK(batch.runs().capacity() == run_capacity);
    CHECK(batch.vertices().data() == storage);

    // Rebuilding the same frame yields identical data in the same storage.
    scene.collectible_markers = &long_arc;
    build_world_marker_batch(batch, scene, test_scene_colors());
    CHECK(batch.vertices().data() == storage);
    REQUIRE(batch.vertices().size() == first_frame.size());
    for (std::size_t i = 0; i < first_frame.size(); ++i) {
        CHECK(batch.vertices()[i].position == first_frame[i].position);
        CHECK(batch.vertices()[i].color == first_frame[i].color);
    }
}

TEST_CASE("world marker glow and appends extend runs only on equal state") {
    world_marker_batch batch;
    batch.append_quad(glm::mat4(1.0f), glm::vec3(0.1f, 0.2f, 0.3f), 0.5f, true);
    batch.append_disc(glm::mat4(1.0f), glm::vec3(0.4f, 0.5f, 0.6f), 0.0f, true);
    batch.append_disc(glm::mat4(1.0f), glm::vec3(0.7f, 0.8f, 0.9f), 0.0f, false);
    batch.append_quad(glm::mat4(1.0f), glm::vec3(1.0f), 0.0f, true);

    CHECK(batch.vertices()[0].color == glm::vec3(0.1f, 0.2f, 0.3f));
    CHECK(batch.vertices()[0].glow == 0.5f);
    CHECK(batch.vertices()[quad_vertex_count].color == glm::vec3(0.4f, 0.5f, 0.6f));
    CHECK(batch.vertices()[quad_vertex_count].glow == 0.0f);
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

// The unit cylinder primitive_mesh should build, written out independently.
std::vector<float> expected_cylinder_vertices(const int segments) {
    std::vector<float> vertices;
    const float pi = glm::pi<float>();
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

// The unit sphere primitive_mesh should build, written out independently.
std::vector<float> expected_sphere_vertices(const int latitude_segments, const int longitude_segments) {
    std::vector<float> vertices;
    const float pi = glm::pi<float>();
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

TEST_CASE("another player is a figure on foot, sat down in a cart, their group in their cap") {
    world_marker_batch batch;
    render_remote_avatar avatar;
    avatar.position = glm::vec3(10.0f, 2.0f, -4.0f);
    avatar.player_id = 3;
    append_remote_avatar(batch, avatar, 1.65f, 0.05f, test_scene_colors());
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

    // Sat, hands on the wheel or in the lap: the cart is drawn on its own.
    for (const bool at_wheel : {true, false}) {
        batch.clear();
        avatar.seated = true;
        avatar.at_wheel = at_wheel;
        append_remote_avatar(batch, avatar, 1.65f, 0.05f, test_scene_colors());
        CHECK(batch.vertices().size() == remote_seated_vertex_count());
    }

    batch.clear();
    avatar.seated = false;
    avatar.group_id = 2;
    append_remote_avatar(batch, avatar, 1.65f, 0.05f, test_scene_colors());
    CHECK(batch.vertices().size() == remote_figure_vertex_count());
    CHECK(group_highlight(test_scene_colors().figures, 2) != group_highlight(test_scene_colors().figures, 3));
    CHECK(remote_tint(test_scene_colors().figures, 1) != remote_tint(test_scene_colors().figures, 2));
}

TEST_CASE("another player holds my club at their ball, and what they smoke and drink at their mouth") {
    render_remote_avatar avatar;
    avatar.position = glm::vec3(10.0f, 2.0f, -4.0f);
    render_club_pose pose;
    pose.ball_position = glm::vec3(10.5f, 2.0f, -4.0f);
    pose.aim_angle = 0.3f;
    pose.raise = 0.6f;
    pose.club_model = "club_iron";
    avatar.swing = pose;
    world_marker_batch batch;
    append_remote_avatar(batch, avatar, 1.65f, 0.05f, test_scene_colors());
    CHECK(batch.vertices().size() == remote_figure_vertex_count());

    CHECK(!remote_prop_holder(avatar, 1.65f, test_props()).has_value());
    avatar.smoking = render_item_use{"cigarette", 0.5f};
    avatar.drinking = render_item_use{"beer_can", 1.0f};
    const std::optional<prop_holder> holder = remote_prop_holder(avatar, 1.65f, test_props());
    REQUIRE(holder.has_value());
    prop_batch props;
    append_held_props(props, *holder, test_scene_colors().glow);
    REQUIRE(!props.opaque().empty());
    // At their face, not at arm's length in front of a camera.
    const glm::vec3 head(holder->frame[3]);
    for (const std::vector<prop_vertex>* vertices : {&props.opaque(), &props.blended()}) {
        for (const prop_vertex& vertex : *vertices) {
            CHECK(glm::length(vertex.position - head) < 0.8f);
            CHECK(vertex.position.y > avatar.position.y + 1.0f);
        }
    }
    // The cigarette (drawn first) starts at their lips: just outside the
    // head, below its middle, in front of it.
    const glm::vec3 filter(holder->frame * glm::vec4(holder->grip.smokable_filter, 1.0f));
    const glm::vec3 from_head = filter - head;
    CHECK(glm::length(from_head) < 0.2f);
    CHECK(from_head.y < 0.0f);
    CHECK(glm::dot(from_head, yaw_direction(avatar.yaw)) > 0.1f);
    // Both have their smoke and beer (blended) and their solid parts.
    CHECK(!props.blended().empty());
    // A model they hold that is not in the catalog draws nothing.
    avatar.smoking = render_item_use{"no_such_model", 0.5f};
    avatar.drinking.reset();
    CHECK(!remote_prop_holder(avatar, 1.65f, test_props()).has_value());

    // Sat in a cart their head is where it is on foot.
    avatar.smoking = render_item_use{"cigarette", 0.5f};
    avatar.seated = true;
    const std::optional<prop_holder> driving = remote_prop_holder(avatar, 1.65f, test_props());
    REQUIRE(driving.has_value());
    check_near(glm::vec3(driving->frame[3]), head);
}

TEST_CASE("a whole cart stands on the ground, roofed over a seated head, its wheel before the driver's seat on its left") {
    prop_batch batch;
    const render_cart cart{glm::vec3(10.0f, 2.0f, -4.0f), 0.0f, false, 0.0f};
    const prop_model& model = test_props().at("golf_cart");
    const float seat_offset = model.point("steering_wheel").x;
    append_cart_model(batch, cart, model, test_scene_colors().glow);
    CHECK(batch.opaque().size() == cart_vertex_count(model));
    float lowest = 1000.0f;
    float highest = -1000.0f;
    glm::vec3 wheel_sum(0.0f);
    glm::vec3 rim_low(1000.0f);
    glm::vec3 rim_high(-1000.0f);
    std::size_t wheel_vertices = 0;
    for (const prop_vertex& vertex : batch.opaque()) {
        lowest = std::min(lowest, vertex.position.y);
        highest = std::max(highest, vertex.position.y);
        if (vertex.color == test_props().at("golf_cart").color("rubber") && vertex.position.y > cart.position.y + 0.8f) {
            wheel_sum += vertex.position;
            rim_low = glm::min(rim_low, vertex.position);
            rim_high = glm::max(rim_high, vertex.position);
            ++wheel_vertices;
        }
    }
    CHECK(lowest >= cart.position.y - 0.01f);
    CHECK(lowest <= cart.position.y + 0.05f);
    CHECK(highest <= cart.position.y + 2.1f);
    // The steering wheel: on the cart's left (yaw_left), as far out as the
    // seat, ahead of it at a seated driver's chest.
    REQUIRE(wheel_vertices > 0U);
    const glm::vec3 wheel = wheel_sum / static_cast<float>(wheel_vertices);
    CHECK(near(glm::dot(wheel - cart.position, yaw_left(yaw_direction(cart.yaw))), seat_offset, 0.02f));
    CHECK(wheel.z > cart.position.z + 0.3f);
    CHECK(wheel.y > cart.position.y + 0.9f);
    CHECK(wheel.y < cart.position.y + 1.3f);
    // Its rim faces the driver, leaning back from upright: taller than it is
    // deep (never lying flat), yet deep (never stood straight up).
    const glm::vec3 rim = rim_high - rim_low;
    CHECK(rim.x > 0.35f);
    CHECK(rim.y > rim.z);
    CHECK(rim.z > 0.15f);

    // It turns with the steering: a spoke swings up off the level.
    const auto highest_trim = [&](const float steer) {
        prop_batch turned;
        append_cart_model(turned, render_cart{cart.position, cart.yaw, true, steer}, model, test_scene_colors().glow);
        CHECK(turned.opaque().size() == cart_vertex_count(model));
        float top = -1000.0f;
        for (const prop_vertex& vertex : turned.opaque()) {
            if (vertex.color == test_props().at("golf_cart").color("trim")) {
                top = std::max(top, vertex.position.y);
            }
        }
        return top;
    };
    CHECK(highest_trim(1.0f) > highest_trim(0.0f) + 0.05f);
    CHECK(highest_trim(-1.0f) > highest_trim(0.0f) + 0.05f);
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
    build_world_marker_batch(batch, scene, test_scene_colors());
    const std::size_t sphere = make_sphere_positions(primitive_sphere_latitude_segments, primitive_sphere_longitude_segments).size();
    CHECK(batch.vertices().size() == 2U * remote_figure_vertex_count() + 3U * sphere);
}

TEST_CASE("a cart tilts with the ground under its wheels, its tyres standing a little clear of it") {
    const auto lowest_gap = [](const render_cart& cart, const std::function<float(const glm::vec3&)>& ground) {
        prop_batch batch;
        append_cart_model(batch, cart, test_props().at("golf_cart"), test_scene_colors().glow);
        float gap = 1000.0f;
        for (const prop_vertex& vertex : batch.opaque()) {
            gap = std::min(gap, vertex.position.y - ground(vertex.position));
        }
        return gap;
    };

    const auto flat = [](const glm::vec3&) { return 2.0f; };
    const render_cart level = cart_on_ground(glm::vec3(10.0f, 0.0f, -4.0f), 0.7f, test_props().at("golf_cart"), flat);
    CHECK(near(level.up, glm::vec3(0.0f, 1.0f, 0.0f)));
    CHECK(level.position.y > 2.0f);
    CHECK(level.position.y < 2.06f);
    CHECK(lowest_gap(level, flat) > 0.0f);

    // Up a slope rising ahead of it and to its left, it leans back and right.
    const auto slope = [](const glm::vec3& point) { return 0.3f * point.z + 0.15f * point.x; };
    const render_cart climbing = cart_on_ground(glm::vec3(0.0f), 0.0f, test_props().at("golf_cart"), slope);
    const glm::vec3 slope_normal = glm::normalize(glm::vec3(-0.15f, 1.0f, -0.3f));
    CHECK(glm::dot(climbing.up, slope_normal) > 0.999f);
    const float gap = lowest_gap(climbing, slope);
    CHECK(gap > 0.0f);
    CHECK(gap < 0.08f);

    // A bump under one wheel lifts the cart over it rather than through it.
    const auto bump = [](const glm::vec3& point) {
        return 0.25f * std::max(0.0f, 1.0f - glm::length(glm::vec2(point.x - 0.66f, point.z - 1.0f)) / 0.5f);
    };
    for (const float yaw : {0.0f, 1.2f, -2.5f}) {
        CHECK(lowest_gap(cart_on_ground(glm::vec3(0.0f), yaw, test_props().at("golf_cart"), bump), bump) > 0.0f);
    }

    // A small sharp lump under a wheel hardly tips it: it rests on the
    // ground smoothed round its wheels.
    const auto lump = [](const glm::vec3& point) {
        return 0.1f * std::max(0.0f, 1.0f - glm::length(glm::vec2(point.x, point.z) - glm::vec2(0.66f, 1.0f)) / 0.15f);
    };
    const render_cart lumped = cart_on_ground(glm::vec3(0.0f), 0.0f, test_props().at("golf_cart"), lump);
    CHECK(lumped.up.y > std::cos(glm::radians(0.5f)));
    CHECK(lowest_gap(lumped, lump) > 0.0f);
}

TEST_CASE("the club's head never cuts into the ball, however the face turns or leans, for either hand") {
    const prop_model& club = test_props().at("club_iron");
    constexpr float radius = 0.10f;
    for (const bool left : {false, true}) {
        for (float turn = -30.0f; turn <= 30.0f; turn += 10.0f) {
            for (const float lean : {-6.0f, 0.0f, 6.0f, 12.0f}) {
                render_club_pose pose;
                pose.ball_position = glm::vec3(1.0f, 0.2f + radius, 2.0f);
                pose.aim_angle = 0.4f;
                pose.face_turn = glm::radians(turn);
                pose.shaft_lean = glm::radians(lean);
                pose.left_handed = left;
                prop_batch batch;
                append_swing_club(batch, pose, radius, club, test_scene_colors().glow);
                float nearest = 1000.0f;
                for (const prop_vertex& vertex : batch.opaque()) {
                    nearest = std::min(nearest, glm::length(vertex.position - pose.ball_position));
                }
                CHECK(nearest >= radius * 0.98f);
                // Yet soled right behind it, its face's middle at the ball.
                const glm::vec3 face(swing_club_frame(pose, radius, club) * glm::vec4(club.point("face"), 1.0f));
                CHECK(glm::length(face - pose.ball_position) < radius * 1.3f);
            }
        }
    }
}
