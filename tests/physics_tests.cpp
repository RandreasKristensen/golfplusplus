#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "game/game_tuning.h"
#include "physics/ball_physics.h"
#include "physics/collision.h"
#include "physics/terrain.h"
#include "physics/wind.h"

#include <cmath>

#include <glm/geometric.hpp>

namespace {
bool near_vec3(const glm::vec3& a, const glm::vec3& b, const float eps = 1e-5f) {
    return glm::length(a - b) <= eps;
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
    CHECK(r.position.y == 0.0f);
    CHECK(r.velocity.y > 0.0f);
    CHECK(r.velocity.x < 2.0f);
    CHECK(r.velocity.z < 3.0f);
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

    CHECK(r1.position.y > 4.9f);
    CHECK(r1.velocity.y > ball.velocity.y);
    CHECK(near_vec3(r1.position, r2.position));
    CHECK(near_vec3(r1.velocity, r2.velocity));
}
