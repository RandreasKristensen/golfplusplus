#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "physics/ball_physics.h"
#include "physics/collision.h"
#include "physics/ground_mesh.h"
#include "physics/terrain.h"
#include "physics/tree_collision.h"
#include "physics/wind.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include "test_support.h"

namespace {
physics_tuning test_physics() {
    physics_tuning physics;
    physics.drag_coeff = 0.004f;
    physics.magnus_coeff = 0.0004f;
    physics.spin_decay = 0.6f;
    return physics;
}

wind_tuning test_wind() {
    wind_tuning wind;
    wind.seed_phase_scale = 0.01f;
    wind.base_speed = 2.0f;
    wind.speed_variation = 1.5f;
    wind.speed_time_scale = 0.2f;
    wind.angle_variation = 0.7f;
    wind.angle_time_scale = 0.11f;
    wind.phase_angle_scale = 1.3f;
    return wind;
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

    const ball_state b2 = step_ball_flight(b, w, 0.016f, test_physics());
    CHECK(glm::length(b2.velocity) < glm::length(b.velocity));
}

TEST_CASE("step is deterministic") {
    ball_state b;
    b.velocity = glm::vec3(10.0f, 5.0f, -3.0f);
    b.spin = glm::vec3(0.2f, 0.1f, 0.0f);
    b.position = glm::vec3(1.0f, 2.0f, 3.0f);

    wind_state w;
    w.velocity = glm::vec3(0.0f, 0.0f, 0.0f);

    const ball_state r1 = step_ball_flight(b, w, 0.016f, test_physics());
    const ball_state r2 = step_ball_flight(b, w, 0.016f, test_physics());

    CHECK(near(r1.position, r2.position));
    CHECK(near(r1.velocity, r2.velocity));
}

TEST_CASE("wind is deterministic") {
    const wind_state w1 = sample_wind(42U, 10.0f, test_wind());
    const wind_state w2 = sample_wind(42U, 10.0f, test_wind());
    CHECK(near(w1.velocity, w2.velocity));
}

TEST_CASE("wind varies with time and seed") {
    const wind_state w_time_a = sample_wind(42U, 0.0f, test_wind());
    const wind_state w_time_b = sample_wind(42U, 3.0f, test_wind());
    CHECK(!near(w_time_a.velocity, w_time_b.velocity));

    const wind_state w_seed_a = sample_wind(1U, 0.0f, test_wind());
    const wind_state w_seed_b = sample_wind(2U, 0.0f, test_wind());
    CHECK(!near(w_seed_a.velocity, w_seed_b.velocity));
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

    const terrain_mesh mesh = plain_terrain_mesh(terrain);

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

    const terrain_mesh mesh = plain_terrain_mesh(terrain);
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
        CHECK(near(center.position.x, 0.0f, 0.001f));
        CHECK(near(center.position.y, center.position.z * 0.5f, 0.001f));
        ++checked_sections;
    }
    CHECK(checked_sections >= terrain.sample_count);
}

TEST_CASE("bunkers and greens win over the water around them") {
    const material_zone green_zone = circle_zone(material_zone_type::green, glm::vec3(0.0f, 0.0f, 10.0f), 6.0f);
    const material_zone bunker_zone = circle_zone(material_zone_type::bunker, glm::vec3(5.0f, 0.0f, 10.0f), 2.0f);
    const material_zone water_zone = ellipse_zone(material_zone_type::water, glm::vec3(0.0f, 0.0f, 10.0f), 25.0f, 30.0f);

    const std::vector<material_zone> zones = {water_zone, green_zone, bunker_zone};
    CHECK(zone_material_at(zones, glm::vec3(0.0f, 0.0f, 10.0f)) == terrain_material::green);
    CHECK(zone_material_at(zones, glm::vec3(5.5f, 0.0f, 10.0f)) == terrain_material::bunker);
    CHECK(zone_material_at(zones, glm::vec3(-10.0f, 0.0f, 10.0f)) == terrain_material::water);
    CHECK(!zone_material_at(zones, glm::vec3(-30.0f, 0.0f, 10.0f)));
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

    const terrain_mesh mesh = plain_terrain_mesh(terrain);
    CHECK(mesh.cross_section_count == 9);
    if (mesh.cross_section_count != 9) {
        return;
    }

    const terrain_vertex edge = mesh.vertices[0];
    const terrain_vertex center = mesh.vertices[4];
    CHECK(center.material == terrain_material::fairway);
    CHECK(edge.material == terrain_material::rough);
}

TEST_CASE("zones decide the material by their exact shape, on the ribbon and past it") {
    terrain_spline terrain;
    terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 20.0f)
    };
    terrain.width = 10.0f;
    terrain.fairway_width = 6.0f;
    terrain.sample_count = 12;

    // A green on the last control point, wider than the ribbon.
    const material_zone green_zone = circle_zone(material_zone_type::green, glm::vec3(0.0f, 0.0f, 20.0f), 8.0f);
    const std::vector<material_zone> zones = {green_zone};

    const std::vector<terrain_mesh> holes{build_terrain_mesh(terrain)};
    for (const terrain_vertex& vertex : holes[0].vertices) {
        CHECK((vertex.material == terrain_material::fairway || vertex.material == terrain_material::rough));
    }

    const auto material_at = [&](const glm::vec3& position) {
        const std::optional<terrain_sample> on_hole = sample_holes(holes, position);
        return surface_material(zones, on_hole ? &*on_hole : nullptr, position);
    };
    CHECK(material_at(glm::vec3(0.0f, 0.0f, 20.0f)) == terrain_material::green);
    CHECK(material_at(glm::vec3(4.5f, 0.0f, 20.0f)) == terrain_material::green);   // in the ribbon's rough
    CHECK(material_at(glm::vec3(7.5f, 0.0f, 20.0f)) == terrain_material::green);   // past the ribbon
    CHECK(material_at(glm::vec3(0.0f, 0.0f, 27.5f)) == terrain_material::green);   // past the ribbon's end
    CHECK(material_at(glm::vec3(0.0f, 0.0f, 5.0f)) == terrain_material::fairway);
    CHECK(material_at(glm::vec3(4.5f, 0.0f, 5.0f)) == terrain_material::rough);
    CHECK(material_at(glm::vec3(30.0f, 0.0f, 5.0f)) == terrain_material::rough);
}

namespace {
// Every overlay vertex and triangle centre is on the ground it was cut from,
// and inside `zone`.
void check_overlay_on_ground(const terrain_mesh& overlay, const terrain_mesh& ground, const material_zone& zone,
                             const terrain_material material) {
    const auto on_ground = [&ground](const glm::vec3& point) {
        return near(point.y, sample_terrain_anchor(ground, point, point.y).point.y, 0.0005f);
    };
    for (const terrain_vertex& vertex : overlay.vertices) {
        CHECK(vertex.material == material);
        CHECK(on_ground(vertex.position));
        const std::optional<float> distance = zone_normalized_distance(zone, vertex.position);
        CHECK(distance.value_or(0.0f) <= 1.0001f);
    }
    for (std::size_t i = 0; i + 2U < overlay.indices.size(); i += 3U) {
        const glm::vec3 centre = (overlay.vertices[overlay.indices[i]].position + overlay.vertices[overlay.indices[i + 1U]].position
                                  + overlay.vertices[overlay.indices[i + 2U]].position) / 3.0f;
        CHECK(on_ground(centre));
    }
}
}

TEST_CASE("material overlay lies on the downhill ground it is cut from") {
    terrain_spline terrain;
    terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, -6.0f, 20.0f)
    };
    terrain.width = 12.0f;
    terrain.sample_count = 20;

    const material_zone bunker_zone = circle_zone(material_zone_type::bunker, glm::vec3(0.0f, 0.0f, 10.0f), 2.0f);
    const terrain_mesh ground = plain_terrain_mesh(terrain);
    const terrain_mesh overlay = build_material_overlay_mesh(ground, {bunker_zone}, 1.0f);

    REQUIRE(overlay.indices.size() >= 3U);
    check_overlay_on_ground(overlay, ground, bunker_zone, terrain_material::bunker);
    bool found_sample_below_authored_height = false;
    for (const terrain_vertex& vertex : overlay.vertices) {
        found_sample_below_authored_height = found_sample_below_authored_height || vertex.position.y < -0.5f;
    }
    CHECK(found_sample_below_authored_height);
}

TEST_CASE("turned ellipse material overlay covers its shape on the ground") {
    terrain_spline terrain;
    terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, -6.0f, 20.0f)
    };
    terrain.width = 12.0f;
    terrain.sample_count = 20;

    // 4 m long, 2 m wide, turned a quarter so its long axis runs along z.
    material_zone water_zone = ellipse_zone(material_zone_type::water, glm::vec3(0.0f, 0.0f, 10.0f), 2.0f, 1.0f);
    water_zone.rotation = glm::radians(90.0f);

    const terrain_mesh ground = plain_terrain_mesh(terrain);
    const terrain_mesh overlay = build_material_overlay_mesh(ground, {water_zone}, 1.0f);

    REQUIRE(overlay.indices.size() >= 3U);
    check_overlay_on_ground(overlay, ground, water_zone, terrain_material::water);
    float min_x = 1.0e9f, max_x = -1.0e9f, min_z = 1.0e9f, max_z = -1.0e9f;
    for (const terrain_vertex& vertex : overlay.vertices) {
        min_x = std::min(min_x, vertex.position.x);
        max_x = std::max(max_x, vertex.position.x);
        min_z = std::min(min_z, vertex.position.z);
        max_z = std::max(max_z, vertex.position.z);
    }
    CHECK(near(min_x, -1.0f, 0.01f));
    CHECK(near(max_x, 1.0f, 0.01f));
    CHECK(near(min_z, 8.0f, 0.01f));
    CHECK(near(max_z, 12.0f, 0.01f));
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

    const terrain_sample sample = sample_spline(terrain, glm::vec3(1.0f, 0.0f, 10.0f), -2.0f);

    CHECK(sample.triangle_index >= 0);
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

    const terrain_sample sample = sample_spline(terrain, glm::vec3(0.0f, 0.0f, 10.0f), -2.0f);

    CHECK(sample.triangle_index >= 0);
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

    const terrain_mesh mesh = plain_terrain_mesh(terrain);
    CHECK(mesh.section_count > 8);
    if (mesh.section_count <= 8) {
        return;
    }

    const int boundary_section = mesh.section_count / 2;
    const glm::vec3 boundary = mesh.vertices[static_cast<std::size_t>(boundary_section * mesh.cross_section_count + 4)].position;
    const terrain_sample before = sample_terrain_mesh(mesh, boundary + glm::vec3(0.2f, 0.0f, -0.02f), -10.0f);
    const terrain_sample after = sample_terrain_mesh(mesh, boundary + glm::vec3(0.2f, 0.0f, 0.02f), -10.0f);

    CHECK(before.triangle_index >= 0);
    CHECK(after.triangle_index >= 0);
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

    const terrain_sample sample = sample_spline(terrain, glm::vec3(9.0f, 0.0f, 10.0f), -5.0f);

    CHECK(sample.triangle_index >= 0);
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

    const terrain_mesh mesh = plain_terrain_mesh(terrain);
    const glm::vec3 authored(12.0f, 0.0f, 10.0f);
    const terrain_sample sample = sample_terrain_anchor(mesh, authored, -5.0f);

    CHECK(sample.triangle_index >= 0);
    CHECK(!sample.inside_surface);
    CHECK(near(sample.point.x, authored.x));
    CHECK(near(sample.point.z, authored.z));
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

    const terrain_sample downhill_sample = sample_terrain_anchor(plain_terrain_mesh(downhill),
                                                                 glm::vec3(12.0f, 0.0f, 10.0f),
                                                                 0.0f);
    const terrain_sample uphill_sample = sample_terrain_anchor(plain_terrain_mesh(uphill),
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

    const terrain_mesh mesh = plain_terrain_mesh(terrain);
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

TEST_CASE("the ground around a lone hole follows the hole, on it and past its edge") {
    terrain_spline terrain;
    terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, -6.0f, 20.0f)
    };
    terrain.width = 10.0f;
    terrain.sample_count = 20;

    const std::vector<terrain_mesh> holes{plain_terrain_mesh(terrain)};
    const terrain_mesh ground = build_ground(holes, {}, nullptr, ground_settings{1.0f, 10.0f, 0.0f});

    // On the hole the ground matches the ribbon; beside it, the edge height.
    const glm::vec3 on_hole(0.0f, 0.0f, 10.0f);
    CHECK(near(sample_terrain_mesh(ground, on_hole, 0.0f).point.y, sample_terrain_mesh(holes[0], on_hole, 0.0f).point.y, 0.05f));
    const glm::vec3 beside(8.0f, 0.0f, 10.0f);
    const float edge = sample_terrain_mesh(holes[0], glm::vec3(4.99f, 0.0f, 10.0f), 0.0f).point.y;
    CHECK(near(sample_terrain_mesh(ground, beside, 0.0f).point.y, edge, 0.05f));
    CHECK(sample_holes(holes, on_hole)->material == terrain_material::fairway);
    CHECK(!sample_holes(holes, beside));
}

TEST_CASE("a banked ribbon tilts across, rising towards its lateral side") {
    terrain_spline terrain;
    terrain.control_points = {glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 40.0f)};
    terrain.bank = {0.1f, 0.1f};
    terrain.width = 20.0f;
    terrain.fairway_width = 20.0f;
    terrain.sample_count = 20;
    const terrain_mesh mesh = plain_terrain_mesh(terrain);

    // Heading +z, the lateral side is -x.
    const float lateral = sample_terrain_mesh(mesh, glm::vec3(-4.0f, 0.0f, 20.0f), 0.0f).point.y;
    const float other = sample_terrain_mesh(mesh, glm::vec3(4.0f, 0.0f, 20.0f), 0.0f).point.y;
    CHECK(near(lateral - other, 0.8f, 0.01f));
}

TEST_CASE("terrain sampling stays on the hinted branch through a crossing overlap") {
    const terrain_mesh mesh = crossing_branch_mesh();
    const glm::vec3 crossing(0.0f, 0.0f, 0.0f);

    terrain_sample lower_hint;
    lower_hint.triangle_index = 0;
    lower_hint.inside_surface = true;
    terrain_sample upper_hint;
    upper_hint.triangle_index = 6;
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
    ball.radius = 0.021335f;
    ball.position = glm::vec3(0.0f, 4.0f, 10.0f);
    ball.velocity = glm::vec3(0.0f, -3.0f, 2.0f);

    const terrain_sample sample = sample_spline(terrain, ball.position, 0.0f);
    const ball_state r1 = resolve_terrain_collision(ball, sample, 0.5f, 0.1f, 1.0f / 60.0f);
    const ball_state r2 = resolve_terrain_collision(ball, sample, 0.5f, 0.1f, 1.0f / 60.0f);

    CHECK(near(glm::dot(r1.position - sample.point, sample.normal), ball.radius));
    CHECK(r1.velocity.y > ball.velocity.y);
    CHECK(near(r1.position, r2.position));
    CHECK(near(r1.velocity, r2.velocity));
}

TEST_CASE("tree trunk collision pushes ball outside and reflects deterministically") {
    tree_body tree;
    tree.base = glm::vec3(0.0f);
    tree.shape.trunk_radius = 0.35f;
    tree.shape.trunk_height = 2.4f;

    ball_state ball;
    ball.radius = 0.1f;
    ball.position = glm::vec3(0.30f, 1.0f, 0.0f);
    ball.velocity = glm::vec3(-2.0f, 0.0f, 0.0f);

    const ball_state r1 = resolve_tree_collision(ball, tree, 0.25f, 0.35f);
    const ball_state r2 = resolve_tree_collision(ball, tree, 0.25f, 0.35f);

    CHECK(r1.position.x >= tree.shape.trunk_radius + ball.radius - 0.0001f);
    CHECK(r1.velocity.x > 0.0f);
    CHECK(near(r1.position, r2.position));
    CHECK(near(r1.velocity, r2.velocity));
}

TEST_CASE("tree leaf cone collision damps and deflects deterministically") {
    tree_body tree;
    tree.base = glm::vec3(0.0f);
    tree.shape.trunk_radius = 0.25f;
    tree.shape.trunk_height = 2.0f;
    tree.shape.leaf_radius = 1.5f;
    tree.shape.leaf_height = 3.0f;

    ball_state ball;
    ball.radius = 0.1f;
    ball.position = glm::vec3(0.8f, 3.0f, 0.0f);
    ball.velocity = glm::vec3(-4.0f, 0.0f, 0.0f);

    const ball_state r1 = resolve_tree_collision(ball, tree, 0.2f, 0.5f);
    const ball_state r2 = resolve_tree_collision(ball, tree, 0.2f, 0.5f);

    CHECK(glm::length(r1.velocity) < glm::length(ball.velocity));
    CHECK(r1.position.x > ball.position.x);
    CHECK(near(r1.position, r2.position));
    CHECK(near(r1.velocity, r2.velocity));
}

TEST_CASE("tree collision leaves distant ball unchanged") {
    tree_body tree;
    tree.base = glm::vec3(0.0f);
    tree.shape = tree_shape{0.35f, 2.4f, 1.6f, 3.2f};

    ball_state ball;
    ball.position = glm::vec3(5.0f, 1.0f, 0.0f);
    ball.velocity = glm::vec3(1.0f, 0.0f, 0.0f);

    const ball_state result = resolve_tree_collision(ball, tree, 0.25f, 0.35f);
    CHECK(near(result.position, ball.position));
    CHECK(near(result.velocity, ball.velocity));
}

// ---------------------------------------------------------------------------
// Terrain spatial index
//
// The full scan (taken whenever a mesh has no usable index) is the
// reference: clearing spatial_index gives it, and the indexed result must
// match it exactly.
// ---------------------------------------------------------------------------

namespace {
terrain_mesh without_spatial_index(terrain_mesh mesh) {
    mesh.spatial_index = terrain_mesh_index{};
    return mesh;
}

bool same_terrain_sample(const terrain_sample& a, const terrain_sample& b) {
    return a.point == b.point
        && a.normal == b.normal
        && a.distance_from_center == b.distance_from_center
        && a.triangle_index == b.triangle_index
        && a.material == b.material
        && a.inside_surface == b.inside_surface;
}

terrain_spline index_test_spline() {
    terrain_spline terrain;
    terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(6.0f, 1.5f, 20.0f),
        glm::vec3(-4.0f, 0.5f, 40.0f),
        glm::vec3(2.0f, 2.0f, 60.0f)
    };
    terrain.width = 22.0f;
    terrain.fairway_width = 10.0f;
    terrain.sample_count = 40;
    return terrain;
}

std::vector<material_zone> index_test_zones() {
    return {circle_zone(material_zone_type::green, glm::vec3(2.0f, 0.0f, 58.0f), 6.0f),
            circle_zone(material_zone_type::bunker, glm::vec3(5.0f, 0.0f, 44.0f), 4.0f),
            circle_zone(material_zone_type::water, glm::vec3(-3.0f, 0.0f, 26.0f), 5.0f)};
}

terrain_mesh index_test_mesh() {
    return build_terrain_mesh(index_test_spline());
}
}

TEST_CASE("terrain spatial index is built by the mesh builders") {
    const terrain_mesh mesh = index_test_mesh();
    CHECK(mesh.indices.size() >= 3U);
    if (mesh.indices.size() < 3U) {
        return;
    }

    CHECK(mesh.spatial_index.cells_x > 0);
    CHECK(mesh.spatial_index.cells_z > 0);
    CHECK(mesh.spatial_index.vertex_count == static_cast<uint32_t>(mesh.vertices.size()));
    CHECK(mesh.spatial_index.triangle_count == static_cast<uint32_t>(mesh.indices.size() / 3U));
    CHECK(mesh.spatial_index.cell_starts.size()
          == static_cast<std::size_t>(mesh.spatial_index.cells_x) * static_cast<std::size_t>(mesh.spatial_index.cells_z) + 1U);
    CHECK(mesh.spatial_index.cell_triangles.size() >= mesh.indices.size() / 3U);

    const terrain_mesh ground = build_ground({mesh}, index_test_zones(), nullptr, ground_settings{8.0f, 12.0f, 0.0f});
    CHECK(ground.spatial_index.cells_x > 0);
    CHECK(ground.spatial_index.triangle_count == static_cast<uint32_t>(ground.indices.size() / 3U));

    const terrain_mesh overlay = build_material_overlay_mesh(ground, index_test_zones(), 1.0f);
    CHECK(overlay.spatial_index.cells_x > 0);
    CHECK(overlay.spatial_index.triangle_count == static_cast<uint32_t>(overlay.indices.size() / 3U));
}

TEST_CASE("indexed terrain sampling matches the full scan across the whole surface") {
    const terrain_mesh mesh = index_test_mesh();
    const terrain_mesh reference_mesh = without_spatial_index(mesh);
    CHECK(mesh.indices.size() >= 3U);
    if (mesh.indices.size() < 3U) {
        return;
    }

    int inside_samples = 0;
    int outside_samples = 0;
    int fairway_samples = 0;
    int rough_samples = 0;
    int mismatches = 0;
    long long indexed_triangles = 0;
    long long reference_triangles = 0;
    int sample_count = 0;

    for (int xi = -14; xi <= 14; ++xi) {
        for (int zi = -4; zi <= 34; ++zi) {
            const glm::vec3 position(static_cast<float>(xi) * 1.75f, 0.0f, static_cast<float>(zi) * 2.0f);
            const float fallback_y = (xi + zi) % 2 == 0 ? 0.0f : -3.0f;

            const terrain_sample indexed = sample_terrain_mesh(mesh, position, fallback_y);
            const terrain_sample reference = sample_terrain_mesh(reference_mesh, position, fallback_y);

            ++sample_count;
            indexed_triangles += indexed.triangles_tested;
            reference_triangles += reference.triangles_tested;
            if (!same_terrain_sample(indexed, reference)) {
                ++mismatches;
            }

            if (reference.inside_surface) {
                ++inside_samples;
            } else {
                ++outside_samples;
            }
            fairway_samples += reference.material == terrain_material::fairway ? 1 : 0;
            rough_samples += reference.material == terrain_material::rough ? 1 : 0;
        }
    }

    CHECK(mismatches == 0);

    // The comparison is only meaningful if the sweep really covered both
    // ribbon materials, the surface interior, and positions off the surface
    // entirely (zone materials are not on the ribbon).
    CHECK(inside_samples > 0);
    CHECK(outside_samples > 0);
    CHECK(fairway_samples > 0);
    CHECK(rough_samples > 0);

    CHECK(indexed_triangles < reference_triangles);
    std::cout << "  [terrain index] samples: " << sample_count
            << " | triangles in mesh: " << (mesh.indices.size() / 3U)
            << " | avg triangles tested full scan: "
            << (static_cast<double>(reference_triangles) / static_cast<double>(sample_count))
            << " | avg triangles tested indexed: "
            << (static_cast<double>(indexed_triangles) / static_cast<double>(sample_count)) << "\n";
}

TEST_CASE("indexed terrain sampling matches the full scan on mesh edges and beyond") {
    const terrain_mesh mesh = index_test_mesh();
    const terrain_mesh reference_mesh = without_spatial_index(mesh);
    CHECK(mesh.indices.size() >= 3U);
    if (mesh.indices.size() < 3U) {
        return;
    }

    float min_x = mesh.vertices.front().position.x;
    float max_x = mesh.vertices.front().position.x;
    float min_z = mesh.vertices.front().position.z;
    float max_z = mesh.vertices.front().position.z;
    for (const terrain_vertex& vertex : mesh.vertices) {
        min_x = std::min(min_x, vertex.position.x);
        max_x = std::max(max_x, vertex.position.x);
        min_z = std::min(min_z, vertex.position.z);
        max_z = std::max(max_z, vertex.position.z);
    }

    std::vector<glm::vec3> probes;
    // Exactly on the bounding edges, just inside, just outside, and far away.
    const float offsets[5] = {-2.0f, -0.001f, 0.0f, 0.001f, 2.0f};
    for (const float offset : offsets) {
        for (int step = 0; step <= 10; ++step) {
            const float t = static_cast<float>(step) / 10.0f;
            const float x = min_x + (max_x - min_x) * t;
            const float z = min_z + (max_z - min_z) * t;
            probes.push_back(glm::vec3(min_x + offset, 0.0f, z));
            probes.push_back(glm::vec3(max_x - offset, 0.0f, z));
            probes.push_back(glm::vec3(x, 0.0f, min_z + offset));
            probes.push_back(glm::vec3(x, 0.0f, max_z - offset));
        }
    }
    probes.push_back(glm::vec3(min_x - 500.0f, 0.0f, min_z - 500.0f));
    probes.push_back(glm::vec3(max_x + 500.0f, 0.0f, max_z + 500.0f));
    probes.push_back(glm::vec3(0.0f, 0.0f, max_z + 250.0f));
    probes.push_back(glm::vec3(min_x - 250.0f, 0.0f, 20.0f));

    int mismatches = 0;
    for (const glm::vec3& probe : probes) {
        const terrain_sample indexed = sample_terrain_mesh(mesh, probe, -1.0f);
        const terrain_sample reference = sample_terrain_mesh(reference_mesh, probe, -1.0f);
        if (!same_terrain_sample(indexed, reference)) {
            ++mismatches;
        }
    }
    CHECK(mismatches == 0);

    // sample_terrain_anchor shares the same path and must agree too.
    int anchor_mismatches = 0;
    for (const glm::vec3& probe : probes) {
        const terrain_sample indexed = sample_terrain_anchor(mesh, probe, 0.0f);
        const terrain_sample reference = sample_terrain_anchor(reference_mesh, probe, 0.0f);
        if (!same_terrain_sample(indexed, reference)) {
            ++anchor_mismatches;
        }
    }
    CHECK(anchor_mismatches == 0);
}

TEST_CASE("indexed terrain sampling matches the full scan on the ground mesh") {
    const terrain_mesh mesh = index_test_mesh();
    const terrain_mesh ground = build_ground({mesh}, index_test_zones(), nullptr, ground_settings{8.0f, 12.0f, 0.0f});
    const terrain_mesh reference_ground = without_spatial_index(ground);
    CHECK(ground.indices.size() >= 3U);
    if (ground.indices.size() < 3U) {
        return;
    }

    int mismatches = 0;
    for (int xi = -16; xi <= 16; ++xi) {
        for (int zi = -6; zi <= 36; ++zi) {
            const glm::vec3 position(static_cast<float>(xi) * 2.25f, 0.0f, static_cast<float>(zi) * 2.25f);
            const terrain_sample indexed = sample_terrain_mesh(ground, position, 0.0f);
            const terrain_sample reference = sample_terrain_mesh(reference_ground, position, 0.0f);
            if (!same_terrain_sample(indexed, reference)) {
                ++mismatches;
            }
        }
    }
    CHECK(mismatches == 0);
}

TEST_CASE("indexed terrain sampling matches the full scan when walking with previous_sample") {
    const terrain_mesh mesh = index_test_mesh();
    const terrain_mesh reference_mesh = without_spatial_index(mesh);
    CHECK(mesh.indices.size() >= 3U);
    if (mesh.indices.size() < 3U) {
        return;
    }

    int mismatches = 0;
    int hinted_samples = 0;
    long long indexed_triangles = 0;
    long long reference_triangles = 0;

    for (int lane = -8; lane <= 8; ++lane) {
        terrain_sample indexed_previous;
        terrain_sample reference_previous;
        bool has_previous = false;

        for (int step = -4; step <= 64; ++step) {
            const glm::vec3 position(static_cast<float>(lane) * 2.5f, 0.0f, static_cast<float>(step));
            const terrain_sample indexed =
                sample_terrain_mesh(mesh, position, -1.0f, has_previous ? &indexed_previous : nullptr);
            const terrain_sample reference =
                sample_terrain_mesh(reference_mesh, position, -1.0f, has_previous ? &reference_previous : nullptr);

            if (has_previous) {
                ++hinted_samples;
                indexed_triangles += indexed.triangles_tested;
                reference_triangles += reference.triangles_tested;
            }
            if (!same_terrain_sample(indexed, reference)) {
                ++mismatches;
            }

            indexed_previous = indexed;
            reference_previous = reference;
            has_previous = true;
        }
    }

    CHECK(mismatches == 0);
    CHECK(hinted_samples > 0);
    CHECK(indexed_triangles < reference_triangles);
    std::cout << "  [terrain index] previous_sample walk: " << hinted_samples
            << " hinted samples | avg triangles tested full scan: "
            << (static_cast<double>(reference_triangles) / static_cast<double>(hinted_samples))
            << " | avg triangles tested indexed: "
            << (static_cast<double>(indexed_triangles) / static_cast<double>(hinted_samples)) << "\n";
}

TEST_CASE("repeated indexed samples with previous_sample are deterministic") {
    const terrain_mesh mesh = index_test_mesh();
    CHECK(mesh.indices.size() >= 3U);
    if (mesh.indices.size() < 3U) {
        return;
    }

    const glm::vec3 probes[6] = {
        glm::vec3(0.0f, 0.0f, 10.0f),   // fairway
        glm::vec3(-3.0f, 0.0f, 26.0f),  // water
        glm::vec3(5.0f, 0.0f, 44.0f),   // bunker
        glm::vec3(2.0f, 0.0f, 58.0f),   // green
        glm::vec3(10.0f, 0.0f, 30.0f),  // rough / ribbon edge
        glm::vec3(40.0f, 0.0f, 30.0f)   // off the surface
    };

    for (const glm::vec3& probe : probes) {
        const terrain_sample seed = sample_terrain_mesh(mesh, probe, 0.0f);
        const terrain_sample first = sample_terrain_mesh(mesh, probe, 0.0f, &seed);
        for (int repeat = 0; repeat < 16; ++repeat) {
            const terrain_sample again = sample_terrain_mesh(mesh, probe, 0.0f, &seed);
            CHECK(same_terrain_sample(again, first));
            CHECK(again.triangles_tested == first.triangles_tested);
        }

        // Feeding a sample back into itself must reach a fixed point too.
        terrain_sample chained = first;
        for (int repeat = 0; repeat < 16; ++repeat) {
            const terrain_sample next = sample_terrain_mesh(mesh, probe, 0.0f, &chained);
            CHECK(same_terrain_sample(next, chained));
            chained = next;
        }
    }
}

TEST_CASE("terrain sampling falls back to the full scan when the index is stale") {
    const terrain_mesh mesh = index_test_mesh();
    CHECK(mesh.indices.size() >= 3U);
    if (mesh.indices.size() < 3U) {
        return;
    }

    // A caller that moves vertices without rebuilding the index leaves a stale
    // index behind. Sampling must notice and stay correct.
    terrain_mesh moved = mesh;
    for (terrain_vertex& vertex : moved.vertices) {
        vertex.position += glm::vec3(120.0f, 0.0f, -75.0f);
    }
    const terrain_mesh moved_reference = without_spatial_index(moved);
    const terrain_mesh moved_reindexed = build_terrain_mesh_index(moved);

    int stale_mismatches = 0;
    int reindexed_mismatches = 0;
    long long stale_triangles = 0;
    long long reindexed_triangles = 0;
    int probes = 0;

    for (int xi = -6; xi <= 6; ++xi) {
        for (int zi = -4; zi <= 20; ++zi) {
            const glm::vec3 position(120.0f + static_cast<float>(xi) * 3.0f,
                                     0.0f,
                                     -75.0f + static_cast<float>(zi) * 3.0f);
            const terrain_sample reference = sample_terrain_mesh(moved_reference, position, 0.0f);
            const terrain_sample stale = sample_terrain_mesh(moved, position, 0.0f);
            const terrain_sample reindexed = sample_terrain_mesh(moved_reindexed, position, 0.0f);
            if (!same_terrain_sample(stale, reference)) {
                ++stale_mismatches;
            }
            if (!same_terrain_sample(reindexed, reference)) {
                ++reindexed_mismatches;
            }
            stale_triangles += stale.triangles_tested;
            reindexed_triangles += reindexed.triangles_tested;
            ++probes;
        }
    }

    CHECK(stale_mismatches == 0);
    CHECK(reindexed_mismatches == 0);
    CHECK(probes > 0);
    // The stale index degrades to the full scan, the rebuilt one does not.
    CHECK(reindexed_triangles < stale_triangles);
}

TEST_CASE("terrain sampling tolerates empty and malformed meshes") {
    const terrain_mesh empty_mesh;
    const terrain_sample empty_sample = sample_terrain_mesh(empty_mesh, glm::vec3(1.0f, 0.0f, 2.0f), 3.0f);
    CHECK(empty_sample.point == glm::vec3(1.0f, 3.0f, 2.0f));
    CHECK(empty_sample.triangle_index == -1);

    terrain_mesh degenerate;
    degenerate.vertices.resize(3);
    for (terrain_vertex& vertex : degenerate.vertices) {
        vertex.position = glm::vec3(4.0f, 1.0f, 4.0f);
        vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    degenerate.indices = {0U, 1U, 2U};
    const terrain_mesh degenerate_indexed = build_terrain_mesh_index(degenerate);
    const terrain_sample degenerate_sample =
        sample_terrain_mesh(degenerate_indexed, glm::vec3(0.0f, 0.0f, 0.0f), -2.0f);
    const terrain_sample degenerate_reference =
        sample_terrain_mesh(degenerate, glm::vec3(0.0f, 0.0f, 0.0f), -2.0f);
    CHECK(same_terrain_sample(degenerate_sample, degenerate_reference));

    // Index count that is not a multiple of three must not be trusted.
    terrain_mesh ragged = index_test_mesh();
    ragged.indices.pop_back();
    const terrain_sample ragged_sample = sample_terrain_mesh(ragged, glm::vec3(0.0f, 0.0f, 10.0f), 0.0f);
    const terrain_sample ragged_reference =
        sample_terrain_mesh(without_spatial_index(ragged), glm::vec3(0.0f, 0.0f, 10.0f), 0.0f);
    CHECK(same_terrain_sample(ragged_sample, ragged_reference));
}

TEST_CASE("terrain spatial index keeps triangles tested per sample near constant as the mesh grows") {
    terrain_spline small_spline = index_test_spline();
    small_spline.sample_count = 24;
    terrain_spline large_spline = index_test_spline();
    large_spline.sample_count = 256;

    const terrain_mesh small_mesh = build_terrain_mesh(small_spline);
    const terrain_mesh large_mesh = build_terrain_mesh(large_spline);
    CHECK(large_mesh.indices.size() > small_mesh.indices.size() * 2U);
    if (small_mesh.indices.size() < 3U || large_mesh.indices.size() < 3U) {
        return;
    }

    long long small_tested = 0;
    long long large_tested = 0;
    long long large_full_scan_tested = 0;
    int probes = 0;
    const terrain_mesh large_reference = without_spatial_index(large_mesh);

    for (int xi = -4; xi <= 4; ++xi) {
        for (int zi = 0; zi <= 28; ++zi) {
            const glm::vec3 position(static_cast<float>(xi), 0.0f, static_cast<float>(zi) * 2.0f);
            small_tested += sample_terrain_mesh(small_mesh, position, 0.0f).triangles_tested;
            large_tested += sample_terrain_mesh(large_mesh, position, 0.0f).triangles_tested;
            large_full_scan_tested += sample_terrain_mesh(large_reference, position, 0.0f).triangles_tested;
            ++probes;
        }
    }

    CHECK(probes > 0);
    if (probes <= 0) {
        return;
    }
    const double small_average = static_cast<double>(small_tested) / static_cast<double>(probes);
    const double large_average = static_cast<double>(large_tested) / static_cast<double>(probes);
    const double full_scan_average = static_cast<double>(large_full_scan_tested) / static_cast<double>(probes);

    std::cout << "  [terrain index] small mesh (" << (small_mesh.indices.size() / 3U) << " tris) avg tested: " << small_average
            << " | large mesh (" << (large_mesh.indices.size() / 3U) << " tris) avg tested indexed: " << large_average
            << " | large mesh avg tested full scan: " << full_scan_average << "\n";

    CHECK(large_average < 32.0);
    CHECK(large_average < full_scan_average * 0.05);
    CHECK(large_average < small_average * 4.0);
}

TEST_CASE("the tree grid hits exactly what testing every tree hits") {
    std::vector<tree_body> trees;
    for (int i = 0; i < 400; ++i) {
        // A scattered wood, some trees close enough to touch each other.
        const float x = static_cast<float>((i * 37) % 200) - 100.0f + static_cast<float>(i % 7) * 0.3f;
        const float z = static_cast<float>((i * 53) % 160) - 80.0f + static_cast<float>(i % 5) * 0.4f;
        trees.push_back(tree_body{glm::vec3(x, static_cast<float>(i % 3), z), tree_shape{0.6f, 4.0f, 4.0f + static_cast<float>(i % 4), 6.0f}});
    }
    const tree_grid grid = build_tree_grid(trees, 16.0f);
    int touched = 0;
    for (int i = 0; i < 2000; ++i) {
        ball_state ball;
        ball.radius = 0.021f;
        ball.position = glm::vec3(static_cast<float>((i * 29) % 240) - 120.0f + 0.13f * static_cast<float>(i % 11),
                                  static_cast<float>(i % 12),
                                  static_cast<float>((i * 41) % 200) - 100.0f + 0.17f * static_cast<float>(i % 13));
        ball.velocity = glm::vec3(static_cast<float>(i % 9) - 4.0f, -1.0f, static_cast<float>(i % 7) - 3.0f);
        const ball_state every = resolve_tree_collisions(ball, trees, 0.3f, 0.2f);
        const ball_state near = resolve_tree_collisions(ball, trees, grid, 0.3f, 0.2f);
        CHECK(every.position == near.position);
        CHECK(every.velocity == near.velocity);
        touched += every.position != ball.position ? 1 : 0;
    }
    CHECK(touched > 50);  // the test reaches into canopies, not only empty air
    CHECK(resolve_tree_collisions(ball_state{}, {}, build_tree_grid({}, 16.0f), 0.3f, 0.2f).position == glm::vec3(0.0f));
}
