#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "game/game_tuning.h"
#include "physics/ball_physics.h"
#include "physics/collision.h"
#include "physics/terrain.h"
#include "physics/tree_collision.h"
#include "physics/wind.h"

#include <cmath>

#include <glm/geometric.hpp>

namespace {
bool near_vec3(const glm::vec3& a, const glm::vec3& b, const float eps = 1e-5f) {
    return glm::length(a - b) <= eps;
}

bool near_float(const float a, const float b, const float eps = 1e-5f) {
    return std::fabs(a - b) <= eps;
}

terrain_mesh crossing_branch_mesh() {
    terrain_mesh mesh;
    mesh.section_count = 2;
    mesh.cross_section_count = 4;
    mesh.width = 2.0f;
    mesh.vertices.resize(16);

    const glm::vec3 normal(0.0f, 1.0f, 0.0f);

    const glm::vec3 branch_a[8] = {
        glm::vec3(-2.0f, 0.0f, -0.8f),
        glm::vec3(-2.0f, 0.0f,  0.8f),
        glm::vec3( 2.0f, 0.0f, -0.8f),
        glm::vec3( 2.0f, 0.0f,  0.8f),
        glm::vec3(-2.0f, 0.0f, -0.8f),
        glm::vec3(-2.0f, 0.0f,  0.8f),
        glm::vec3( 2.0f, 0.0f, -0.8f),
        glm::vec3( 2.0f, 0.0f,  0.8f)
    };

    const glm::vec3 branch_b[8] = {
        glm::vec3(-0.8f, 5.0f, -2.0f),
        glm::vec3( 0.8f, 5.0f, -2.0f),
        glm::vec3(-0.8f, 5.0f,  2.0f),
        glm::vec3( 0.8f, 5.0f,  2.0f),
        glm::vec3(-0.8f, 5.0f, -2.0f),
        glm::vec3( 0.8f, 5.0f, -2.0f),
        glm::vec3(-0.8f, 5.0f,  2.0f),
        glm::vec3( 0.8f, 5.0f,  2.0f)
    };

    for (int i = 0; i < 8; ++i) {
        mesh.vertices[static_cast<std::size_t>(i)].position = branch_a[i];
        mesh.vertices[static_cast<std::size_t>(i)].normal = normal;
        mesh.vertices[static_cast<std::size_t>(i)].distance_from_center = 0.0f;
        mesh.vertices[static_cast<std::size_t>(i)].material = terrain_material::fairway;

        mesh.vertices[static_cast<std::size_t>(i + 8)].position = branch_b[i];
        mesh.vertices[static_cast<std::size_t>(i + 8)].normal = normal;
        mesh.vertices[static_cast<std::size_t>(i + 8)].distance_from_center = 0.0f;
        mesh.vertices[static_cast<std::size_t>(i + 8)].material = terrain_material::green;
    }

    mesh.indices = {
        0U, 1U, 2U,
        1U, 3U, 2U,
        4U, 5U, 6U,
        5U, 7U, 6U,
        8U, 9U, 10U,
        9U, 11U, 10U,
        12U, 13U, 14U,
        13U, 15U, 14U
    };

    return mesh;
}
}

TEST_CASE("ball decelerates under aerodynamic drag") {
    ball_state b;
    b.velocity = glm::vec3(50.0f, 0.0f, 0.0f);
    b.spin = glm::vec3(0.0f, 0.0f, 0.0f);
    b.position = glm::vec3(0.0f, 0.0f, 0.0f);

    wind_state w;
    w.velocity = glm::vec3(0.0f, 0.0f, 0.0f);

    const game_tuning tuning = default_game_tuning();
    const ball_state b2 = step(b, w, 0.016f, tuning.physics);
    CHECK(glm::length(b2.velocity) < glm::length(b.velocity));
}

TEST_CASE("step is deterministic") {
    ball_state b;
    b.velocity = glm::vec3(10.0f, 5.0f, -3.0f);
    b.spin = glm::vec3(0.2f, 0.1f, 0.0f);
    b.position = glm::vec3(1.0f, 2.0f, 3.0f);

    wind_state w;
    w.velocity = glm::vec3(0.0f, 0.0f, 0.0f);

    const game_tuning tuning = default_game_tuning();
    const ball_state r1 = step(b, w, 0.016f, tuning.physics);
    const ball_state r2 = step(b, w, 0.016f, tuning.physics);

    CHECK(near_vec3(r1.position, r2.position));
    CHECK(near_vec3(r1.velocity, r2.velocity));
}

TEST_CASE("wind is deterministic") {
    const game_tuning tuning = default_game_tuning();
    const wind_state w1 = sample_wind(42U, 10.0f, tuning.wind);
    const wind_state w2 = sample_wind(42U, 10.0f, tuning.wind);
    CHECK(near_vec3(w1.velocity, w2.velocity));
}

TEST_CASE("wind varies with time and seed") {
    const game_tuning tuning = default_game_tuning();
    const wind_state w_time_a = sample_wind(42U, 0.0f, tuning.wind);
    const wind_state w_time_b = sample_wind(42U, 3.0f, tuning.wind);
    CHECK(!near_vec3(w_time_a.velocity, w_time_b.velocity));

    const wind_state w_seed_a = sample_wind(1U, 0.0f, tuning.wind);
    const wind_state w_seed_b = sample_wind(2U, 0.0f, tuning.wind);
    CHECK(!near_vec3(w_seed_a.velocity, w_seed_b.velocity));
}

TEST_CASE("ground collision bounces and applies friction") {
    ball_state b;
    b.position = glm::vec3(0.0f, -1.0f, 0.0f);
    b.velocity = glm::vec3(2.0f, -5.0f, 3.0f);

    const ball_state r = resolve_ground_collision(b, 0.0f, 0.8f, 0.3f);
    CHECK(near_float(r.position.y, b.radius));
    CHECK(r.velocity.y > 0.0f);
    CHECK(r.velocity.x < 2.0f);
    CHECK(r.velocity.z < 3.0f);
}

TEST_CASE("ball state carries the real golf ball contract") {
    const ball_state ball;

    CHECK(near_float(ball.radius, 0.021335f));
    CHECK(near_float(ball.mass, 0.04593f));
}

TEST_CASE("terrain mesh is a continuous shared ribbon") {
    terrain_spline terrain;
    terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 10.0f),
        glm::vec3(4.0f, 1.0f, 20.0f)
    };
    terrain.width = 8.0f;
    terrain.sample_count = 12;

    const terrain_mesh mesh = build_terrain_mesh(terrain);

    CHECK(mesh.section_count >= 21);
    CHECK(mesh.cross_section_count == 9);
    if (mesh.section_count < 21 || mesh.cross_section_count != 9) {
        return;
    }
    CHECK(mesh.vertices.size() == static_cast<std::size_t>(mesh.section_count * mesh.cross_section_count));
    CHECK(mesh.indices.size() == static_cast<std::size_t>((mesh.section_count - 1) * (mesh.cross_section_count - 1) * 6));

    const int section = 3;
    const int column = 4;
    const uint32_t shared = static_cast<uint32_t>(section * mesh.cross_section_count + column);
    int shared_uses = 0;
    for (const uint32_t index : mesh.indices) {
        if (index == shared) {
            ++shared_uses;
        }
    }
    CHECK(shared_uses >= 4);
}

TEST_CASE("terrain mesh centerline follows spline height") {
    terrain_spline terrain;
    terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 5.0f, 10.0f),
        glm::vec3(0.0f, 10.0f, 20.0f)
    };
    terrain.width = 8.0f;
    terrain.sample_count = 16;

    const terrain_mesh mesh = build_terrain_mesh(terrain);
    CHECK(mesh.cross_section_count == 9);
    if (mesh.cross_section_count != 9) {
        return;
    }

    for (int section = 0; section < mesh.section_count; ++section) {
        const float t = static_cast<float>(section) / static_cast<float>(mesh.section_count - 1);
        const glm::vec3 spline_point = sample_terrain_spline_point(terrain, t);
        const terrain_vertex center = mesh.vertices[static_cast<std::size_t>(section * mesh.cross_section_count + 4)];
        CHECK(near_vec3(center.position, spline_point, 0.001f));
    }
}

TEST_CASE("terrain zones prioritize water over green") {
    terrain_spline terrain;
    terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 20.0f)
    };
    terrain.width = 10.0f;
    terrain.sample_count = 12;

    material_zone green_zone;
    green_zone.type = material_zone_type::green;
    green_zone.center = glm::vec3(0.0f, 0.0f, 10.0f);
    green_zone.radius = 6.0f;
    green_zone.has_radius = true;

    material_zone water_zone;
    water_zone.type = material_zone_type::water;
    water_zone.center = glm::vec3(0.0f, 0.0f, 10.0f);
    water_zone.radius = 3.0f;
    water_zone.has_radius = true;

    std::vector<material_zone> zones = {green_zone, water_zone};
    terrain_zone_tuning zone_tuning;
    zone_tuning.water_depth = 0.4f;

    const terrain_mesh mesh = build_terrain_mesh(terrain, zones, zone_tuning);
    const terrain_sample sample = sample_terrain_mesh(mesh, glm::vec3(0.0f, 0.0f, 10.0f), -2.0f);

    CHECK(sample.material == terrain_material::water);
}

TEST_CASE("terrain mesh separates fairway from authored rough ribbon") {
    terrain_spline terrain;
    terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 20.0f)
    };
    terrain.width = 20.0f;
    terrain.fairway_width = 10.0f;
    terrain.sample_count = 12;

    const terrain_mesh mesh = build_terrain_mesh(terrain);
    CHECK(mesh.cross_section_count == 9);
    if (mesh.cross_section_count != 9) {
        return;
    }

    const terrain_vertex edge = mesh.vertices[0];
    const terrain_vertex center = mesh.vertices[4];
    CHECK(center.material == terrain_material::fairway);
    CHECK(edge.material == terrain_material::rough);
}

TEST_CASE("terrain zones override rough and fairway materials") {
    terrain_spline terrain;
    terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 20.0f)
    };
    terrain.width = 20.0f;
    terrain.fairway_width = 8.0f;
    terrain.sample_count = 12;

    material_zone water_zone;
    water_zone.type = material_zone_type::water;
    water_zone.center = glm::vec3(10.0f, 0.0f, 0.0f);
    water_zone.radius = 1.0f;
    water_zone.has_radius = true;

    const terrain_mesh mesh = build_terrain_mesh(terrain, {water_zone}, terrain_zone_tuning{});
    CHECK(mesh.cross_section_count == 9);
    if (mesh.cross_section_count != 9) {
        return;
    }

    const terrain_vertex rough_edge = mesh.vertices[0];
    CHECK(rough_edge.material == terrain_material::water);
}

TEST_CASE("spline terrain samples loaded elevation") {
    terrain_spline terrain;
    terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 5.0f, 10.0f),
        glm::vec3(0.0f, 10.0f, 20.0f)
    };
    terrain.width = 8.0f;
    terrain.sample_count = 64;

    const terrain_sample sample = sample_terrain(terrain, glm::vec3(1.0f, 0.0f, 10.0f), -2.0f);

    CHECK(sample.has_spline);
    CHECK(sample.inside_surface);
    CHECK(sample.material == terrain_material::fairway);
    CHECK(sample.point.y > 4.9f);
    CHECK(sample.point.y < 5.1f);
    CHECK(sample.distance_from_center > 0.9f);
    CHECK(sample.normal.y > 0.0f);
    CHECK(sample.triangle_index >= 0);
}

TEST_CASE("terrain sampling refines between coarse samples") {
    terrain_spline terrain;
    terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 10.0f, 100.0f)
    };
    terrain.width = 8.0f;
    terrain.sample_count = 2;

    const terrain_sample sample = sample_terrain(terrain, glm::vec3(0.0f, 0.0f, 10.0f), -2.0f);

    CHECK(sample.has_spline);
    CHECK(sample.point.y > 0.5f);
    CHECK(sample.point.y < 2.5f);
    CHECK(sample.distance_from_center < 0.1f);
}

TEST_CASE("terrain sampling is continuous across section boundaries") {
    terrain_spline terrain;
    terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 5.0f, 10.0f),
        glm::vec3(0.0f, 9.0f, 20.0f),
        glm::vec3(0.0f, 11.0f, 30.0f)
    };
    terrain.width = 10.0f;
    terrain.sample_count = 30;

    const terrain_mesh mesh = build_terrain_mesh(terrain);
    CHECK(mesh.section_count > 8);
    if (mesh.section_count <= 8) {
        return;
    }

    const int boundary_section = mesh.section_count / 2;
    const glm::vec3 boundary = mesh.vertices[static_cast<std::size_t>(boundary_section * mesh.cross_section_count + 4)].position;
    const terrain_sample before = sample_terrain_mesh(mesh, boundary + glm::vec3(0.2f, 0.0f, -0.02f), -10.0f);
    const terrain_sample after = sample_terrain_mesh(mesh, boundary + glm::vec3(0.2f, 0.0f, 0.02f), -10.0f);

    CHECK(before.has_spline);
    CHECK(after.has_spline);
    CHECK(before.inside_surface);
    CHECK(after.inside_surface);
    CHECK(std::abs(before.point.y - after.point.y) < 0.1f);
    CHECK(glm::dot(before.normal, after.normal) > 0.98f);
}

TEST_CASE("terrain sampling clamps outside ribbon as rough") {
    terrain_spline terrain;
    terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 20.0f)
    };
    terrain.width = 8.0f;
    terrain.sample_count = 20;

    const terrain_sample sample = sample_terrain(terrain, glm::vec3(9.0f, 0.0f, 10.0f), -5.0f);

    CHECK(sample.has_spline);
    CHECK(!sample.inside_surface);
    CHECK(sample.material == terrain_material::rough);
    CHECK(sample.point.x < -3.9f || sample.point.x > 3.9f);
    CHECK(sample.distance_from_center > 8.9f);
}

TEST_CASE("terrain sampling stays on the hinted branch through a crossing overlap") {
    const terrain_mesh mesh = crossing_branch_mesh();
    const glm::vec3 crossing(0.0f, 0.0f, 0.0f);

    terrain_sample lower_hint;
    lower_hint.triangle_index = 0;
    lower_hint.has_spline = true;
    lower_hint.inside_surface = true;
    terrain_sample upper_hint;
    upper_hint.triangle_index = 6;
    upper_hint.has_spline = true;
    upper_hint.inside_surface = true;

    const terrain_sample lower_cross = sample_terrain_mesh(mesh, crossing, 0.0f, &lower_hint);
    const terrain_sample upper_cross = sample_terrain_mesh(mesh, crossing, 0.0f, &upper_hint);

    CHECK(lower_hint.inside_surface);
    CHECK(upper_hint.inside_surface);
    CHECK(lower_cross.inside_surface);
    CHECK(upper_cross.inside_surface);
    CHECK(lower_cross.material == terrain_material::fairway);
    CHECK(upper_cross.material == terrain_material::green);
    CHECK(std::abs(lower_cross.point.y - 0.0f) < 0.001f);
    CHECK(std::abs(upper_cross.point.y - 5.0f) < 0.001f);
    CHECK(lower_cross.triangle_index == lower_hint.triangle_index);
    CHECK(upper_cross.triangle_index == upper_hint.triangle_index);
}

TEST_CASE("terrain collision uses spline elevation and is deterministic") {
    terrain_spline terrain;
    terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 5.0f, 10.0f),
        glm::vec3(0.0f, 10.0f, 20.0f)
    };
    terrain.width = 8.0f;
    terrain.sample_count = 64;

    ball_state ball;
    ball.position = glm::vec3(0.0f, 4.0f, 10.0f);
    ball.velocity = glm::vec3(0.0f, -3.0f, 2.0f);

    const terrain_sample sample = sample_terrain(terrain, ball.position, 0.0f);
    const ball_state r1 = resolve_terrain_collision(ball, sample, 0.5f, 0.1f);
    const ball_state r2 = resolve_terrain_collision(ball, sample, 0.5f, 0.1f);

    CHECK(near_float(glm::dot(r1.position - sample.point, sample.normal), ball.radius));
    CHECK(r1.velocity.y > ball.velocity.y);
    CHECK(near_vec3(r1.position, r2.position));
    CHECK(near_vec3(r1.velocity, r2.velocity));
}

TEST_CASE("tree trunk collision pushes ball outside and reflects deterministically") {
    tree_collision_body tree;
    tree.base = glm::vec3(0.0f);
    tree.trunk_radius = 0.35f;
    tree.trunk_height = 2.4f;

    ball_state ball;
    ball.radius = 0.1f;
    ball.position = glm::vec3(0.30f, 1.0f, 0.0f);
    ball.velocity = glm::vec3(-2.0f, 0.0f, 0.0f);

    const ball_state r1 = resolve_tree_collision(ball, tree, 0.25f, 0.35f);
    const ball_state r2 = resolve_tree_collision(ball, tree, 0.25f, 0.35f);

    CHECK(r1.position.x >= tree.trunk_radius + ball.radius - 0.0001f);
    CHECK(r1.velocity.x > 0.0f);
    CHECK(near_vec3(r1.position, r2.position));
    CHECK(near_vec3(r1.velocity, r2.velocity));
}

TEST_CASE("tree leaf cone collision damps and deflects deterministically") {
    tree_collision_body tree;
    tree.base = glm::vec3(0.0f);
    tree.trunk_radius = 0.25f;
    tree.trunk_height = 2.0f;
    tree.leaf_radius = 1.5f;
    tree.leaf_height = 3.0f;

    ball_state ball;
    ball.radius = 0.1f;
    ball.position = glm::vec3(0.8f, 3.0f, 0.0f);
    ball.velocity = glm::vec3(-4.0f, 0.0f, 0.0f);

    const ball_state r1 = resolve_tree_collision(ball, tree, 0.2f, 0.5f);
    const ball_state r2 = resolve_tree_collision(ball, tree, 0.2f, 0.5f);

    CHECK(glm::length(r1.velocity) < glm::length(ball.velocity));
    CHECK(r1.position.x > ball.position.x);
    CHECK(near_vec3(r1.position, r2.position));
    CHECK(near_vec3(r1.velocity, r2.velocity));
}

TEST_CASE("tree collision leaves distant ball unchanged") {
    tree_collision_body tree;
    tree.base = glm::vec3(0.0f);

    ball_state ball;
    ball.position = glm::vec3(5.0f, 1.0f, 0.0f);
    ball.velocity = glm::vec3(1.0f, 0.0f, 0.0f);

    const ball_state result = resolve_tree_collision(ball, tree, 0.25f, 0.35f);
    CHECK(near_vec3(result.position, ball.position));
    CHECK(near_vec3(result.velocity, ball.velocity));
}
