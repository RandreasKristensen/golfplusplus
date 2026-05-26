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

    int checked_sections = 0;
    for (int section = 0; section < mesh.section_count; ++section) {
        const terrain_vertex center = mesh.vertices[static_cast<std::size_t>(section * mesh.cross_section_count + 4)];
        if (center.position.z < -0.001f || center.position.z > 20.001f) {
            continue;
        }
        CHECK(near_float(center.position.x, 0.0f, 0.001f));
        CHECK(near_float(center.position.y, center.position.z * 0.5f, 0.001f));
        ++checked_sections;
    }
    CHECK(checked_sections >= terrain.sample_count);
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

    bool found_water_edge = false;
    for (const terrain_vertex& vertex : mesh.vertices) {
        if (std::abs(vertex.position.x - 10.0f) < 0.001f && std::abs(vertex.position.z) < 0.001f) {
            found_water_edge = found_water_edge || vertex.material == terrain_material::water;
        }
    }
    CHECK(found_water_edge);
}

TEST_CASE("radius material overlay samples downhill terrain height") {
    terrain_spline terrain;
    terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, -6.0f, 20.0f)
    };
    terrain.width = 12.0f;
    terrain.sample_count = 20;

    material_zone bunker_zone;
    bunker_zone.type = material_zone_type::bunker;
    bunker_zone.center = glm::vec3(0.0f, 0.0f, 10.0f);
    bunker_zone.radius = 2.0f;
    bunker_zone.has_radius = true;

    constexpr float lift = 0.045f;
    const terrain_mesh terrain_mesh_data = build_terrain_mesh(terrain);
    const terrain_mesh overlay = build_material_overlay_mesh(terrain_mesh_data, {bunker_zone}, lift);

    CHECK(overlay.vertices.size() == 33U);
    CHECK(overlay.indices.size() == 96U);
    if (overlay.vertices.empty()) {
        return;
    }
    CHECK(near_float(overlay.vertices.front().position.x, bunker_zone.center.x));
    CHECK(near_float(overlay.vertices.front().position.z, bunker_zone.center.z));

    bool found_sample_below_authored_height = false;
    for (const terrain_vertex& vertex : overlay.vertices) {
        CHECK(vertex.material == terrain_material::bunker);
        found_sample_below_authored_height = found_sample_below_authored_height || vertex.position.y < -0.5f;
        CHECK(vertex.position.y < bunker_zone.center.y + lift - 0.5f);
    }
    CHECK(found_sample_below_authored_height);
}

TEST_CASE("bounds material overlay samples downhill terrain height") {
    terrain_spline terrain;
    terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, -6.0f, 20.0f)
    };
    terrain.width = 12.0f;
    terrain.sample_count = 20;

    material_zone water_zone;
    water_zone.type = material_zone_type::water;
    water_zone.bounds_min = glm::vec3(-2.0f, 0.0f, 8.0f);
    water_zone.bounds_max = glm::vec3(2.0f, 0.0f, 12.0f);
    water_zone.has_bounds = true;

    constexpr float lift = 0.045f;
    const terrain_mesh terrain_mesh_data = build_terrain_mesh(terrain);
    const terrain_mesh overlay = build_material_overlay_mesh(terrain_mesh_data, {water_zone}, lift);

    CHECK(overlay.vertices.size() == 36U);
    CHECK(overlay.indices.size() == 150U);
    if (overlay.vertices.empty()) {
        return;
    }
    CHECK(near_float(overlay.vertices.front().position.x, water_zone.bounds_min.x));
    CHECK(near_float(overlay.vertices.front().position.z, water_zone.bounds_min.z));
    CHECK(near_float(overlay.vertices.back().position.x, water_zone.bounds_max.x));
    CHECK(near_float(overlay.vertices.back().position.z, water_zone.bounds_max.z));

    bool found_sample_below_authored_height = false;
    for (const terrain_vertex& vertex : overlay.vertices) {
        CHECK(vertex.material == terrain_material::water);
        found_sample_below_authored_height = found_sample_below_authored_height || vertex.position.y < -0.5f;
        CHECK(vertex.position.y < water_zone.bounds_min.y + lift - 0.5f);
    }
    CHECK(found_sample_below_authored_height);
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

TEST_CASE("terrain anchor sampling preserves authored horizontal position outside ribbon") {
    terrain_spline terrain;
    terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 20.0f)
    };
    terrain.width = 8.0f;
    terrain.sample_count = 20;

    const terrain_mesh mesh = build_terrain_mesh(terrain);
    const glm::vec3 authored(12.0f, 0.0f, 10.0f);
    const terrain_sample sample = sample_terrain_anchor(mesh, authored, -5.0f);

    CHECK(sample.has_spline);
    CHECK(!sample.inside_surface);
    CHECK(near_float(sample.point.x, authored.x));
    CHECK(near_float(sample.point.z, authored.z));
    CHECK(sample.material == terrain_material::rough);
}

TEST_CASE("terrain anchor height follows nearby downhill and uphill terrain outside ribbon") {
    terrain_spline downhill;
    downhill.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, -6.0f, 20.0f)
    };
    downhill.width = 8.0f;
    downhill.sample_count = 20;

    terrain_spline uphill = downhill;
    uphill.control_points[1].y = 6.0f;

    const terrain_sample downhill_sample = sample_terrain_anchor(build_terrain_mesh(downhill),
                                                                 glm::vec3(12.0f, 0.0f, 10.0f),
                                                                 0.0f);
    const terrain_sample uphill_sample = sample_terrain_anchor(build_terrain_mesh(uphill),
                                                               glm::vec3(12.0f, 0.0f, 10.0f),
                                                               0.0f);

    CHECK(downhill_sample.point.y < -1.0f);
    CHECK(uphill_sample.point.y > 1.0f);
}

TEST_CASE("terrain mesh end caps extend beyond first and last control points") {
    terrain_spline terrain;
    terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 40.0f)
    };
    terrain.width = 20.0f;
    terrain.fairway_width = 10.0f;
    terrain.sample_count = 12;

    const terrain_mesh mesh = build_terrain_mesh(terrain);
    CHECK(mesh.cross_section_count == 9);
    if (mesh.cross_section_count != 9 || mesh.vertices.empty()) {
        return;
    }

    const terrain_vertex first_center = mesh.vertices[4];
    const terrain_vertex last_center = mesh.vertices[static_cast<std::size_t>((mesh.section_count - 1) * mesh.cross_section_count + 4)];
    const terrain_vertex first_edge = mesh.vertices[0];

    CHECK(first_center.position.z <= -9.9f);
    CHECK(last_center.position.z >= 49.9f);
    CHECK(first_center.material == terrain_material::fairway);
    CHECK(first_edge.material == terrain_material::rough);
}

TEST_CASE("outer rough apron samples terrain elevation instead of flat ground") {
    terrain_spline terrain;
    terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, -6.0f, 20.0f)
    };
    terrain.width = 10.0f;
    terrain.sample_count = 20;

    const terrain_mesh mesh = build_terrain_mesh(terrain);
    const terrain_mesh apron = build_outer_rough_apron(mesh, terrain.width, 8);

    bool found_below_zero = false;
    for (const terrain_vertex& vertex : apron.vertices) {
        found_below_zero = found_below_zero || vertex.position.y < 0.0f;
        CHECK(vertex.material == terrain_material::rough);
    }
    CHECK(found_below_zero);
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
