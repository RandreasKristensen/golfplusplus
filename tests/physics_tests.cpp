#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "physics/ball_physics.h"
#include "physics/body_collision.h"
#include "physics/collision.h"
#include "physics/ground_mesh.h"
#include "physics/land_relief.h"
#include "physics/terrain.h"
#include "physics/solid.h"
#include "physics/wind.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include "test_support.h"

namespace {
physics_tuning test_physics() {
    return shipped_content().tuning.physics;
}

air_state sea_level_air() {
    return air_at(shipped_content().tuning.physics.air, 0.0f, 15.0f);
}

wind_profile test_profile() {
    return wind_profile{{{0.03f, 0.0f}, {1.0f, 0.6f}, {10.0f, 1.0f}, {40.0f, 1.24f}}};
}
}

TEST_CASE("ball decelerates under aerodynamic drag") {
    ball_state b;
    b.velocity = glm::vec3(50.0f, 0.0f, 0.0f);
    b.spin = glm::vec3(0.0f, 0.0f, 0.0f);
    b.position = glm::vec3(0.0f, 0.0f, 0.0f);
    b.radius = 0.021335f;

    wind_state w;
    w.velocity = glm::vec3(0.0f, 0.0f, 0.0f);

    const ball_state b2 = step_ball_flight(b, w, sea_level_air(), false, 0.016f, test_physics());
    CHECK(glm::length(b2.velocity) < glm::length(b.velocity));
}

TEST_CASE("step is deterministic") {
    ball_state b;
    b.velocity = glm::vec3(10.0f, 5.0f, -3.0f);
    b.spin = glm::vec3(0.2f, 0.1f, 0.0f);
    b.position = glm::vec3(1.0f, 2.0f, 3.0f);

    wind_state w;
    w.velocity = glm::vec3(0.0f, 0.0f, 0.0f);

    const ball_state r1 = step_ball_flight(b, w, sea_level_air(), false, 0.016f, test_physics());
    const ball_state r2 = step_ball_flight(b, w, sea_level_air(), false, 0.016f, test_physics());

    CHECK(near(r1.position, r2.position));
    CHECK(near(r1.velocity, r2.velocity));
}

TEST_CASE("the wind slows towards the ground and is held above the profile") {
    const glm::vec3 wind(5.0f, 0.0f, 0.0f);
    CHECK(wind_at_height(wind, 0.02f, test_profile()).velocity == glm::vec3(0.0f));
    CHECK(near(wind_at_height(wind, 1.0f, test_profile()).velocity, wind * 0.6f));
    CHECK(near(wind_at_height(wind, 5.5f, test_profile()).velocity, wind * 0.8f));
    CHECK(near(wind_at_height(wind, 10.0f, test_profile()).velocity, wind));
    CHECK(near(wind_at_height(wind, 100.0f, test_profile()).velocity, wind * 1.24f));
    CHECK(wind_at_height(wind, 3.0f, wind_profile{}).velocity == glm::vec3(0.0f));
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
        return near(point.y, sample_terrain_mesh(ground, point, point.y).point.y, 0.0005f);
    };
    for (const terrain_vertex& vertex : overlay.vertices) {
        CHECK(vertex.material == material);
        CHECK(on_ground(vertex.position));
        CHECK(zone_distance_outside(zone, vertex.position) < 0.001f);
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
    const terrain_mesh overlay = build_material_overlay_mesh(ground, {bunker_zone});

    REQUIRE(overlay.indices.size() >= 3U);
    check_overlay_on_ground(overlay, ground, bunker_zone, terrain_material::bunker);
    bool found_sample_below_authored_height = false;
    for (const terrain_vertex& vertex : overlay.vertices) {
        found_sample_below_authored_height = found_sample_below_authored_height || vertex.position.y < -0.5f;
    }
    CHECK(found_sample_below_authored_height);
}

TEST_CASE("a zone's material overlay covers its outline on the ground") {
    terrain_spline terrain;
    terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, -6.0f, 20.0f)
    };
    terrain.width = 12.0f;
    terrain.sample_count = 20;

    // 4 m long along z, 2 m wide.
    const material_zone water_zone = ellipse_zone(material_zone_type::water, glm::vec3(0.0f, 0.0f, 10.0f), 1.0f, 2.0f);

    const terrain_mesh ground = plain_terrain_mesh(terrain);
    const terrain_mesh overlay = build_material_overlay_mesh(ground, {water_zone});

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

TEST_CASE("terrain sampling outside the ribbon takes the nearest edge as rough") {
    terrain_spline terrain;
    terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 20.0f)
    };
    terrain.width = 8.0f;
    terrain.sample_count = 20;

    const terrain_mesh mesh = plain_terrain_mesh(terrain);
    const terrain_sample sample = sample_terrain_mesh(mesh, glm::vec3(12.0f, 0.0f, 10.0f), -5.0f);

    CHECK(sample.triangle_index >= 0);
    CHECK(!sample.inside_surface);
    CHECK(sample.material == terrain_material::rough);
}

TEST_CASE("terrain height outside the ribbon follows nearby downhill and uphill terrain") {
    terrain_spline downhill;
    downhill.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, -6.0f, 20.0f)
    };
    downhill.width = 8.0f;
    downhill.sample_count = 20;

    terrain_spline uphill = downhill;
    uphill.control_points[1].y = 6.0f;

    const terrain_sample downhill_sample = sample_terrain_mesh(plain_terrain_mesh(downhill), glm::vec3(12.0f, 0.0f, 10.0f), 0.0f);
    const terrain_sample uphill_sample = sample_terrain_mesh(plain_terrain_mesh(uphill), glm::vec3(12.0f, 0.0f, 10.0f), 0.0f);

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

TEST_CASE("land relief scales heights about the land's mean, and a green's about its own") {
    height_grid land;
    land.cell_size = 1.0f;
    land.columns = 41;
    land.rows = 41;
    for (int row = 0; row < land.rows; ++row) {
        for (int column = 0; column < land.columns; ++column) {
            land.heights.push_back(std::sin(static_cast<float>(column) * 0.7f) * 2.0f + static_cast<float>(row) * 0.1f);
        }
    }
    const material_zone green = circle_zone(material_zone_type::green, glm::vec3(30.0f, 0.0f, 30.0f), 5.0f);
    const std::vector<material_zone> zones{green, circle_zone(material_zone_type::bunker, glm::vec3(10.0f, 0.0f, 10.0f), 5.0f)};

    // Unit scales leave every height exactly as it was.
    CHECK(relieved_land(land, zones, land_relief{1.0f, 1.0f, 3.0f}).heights == land.heights);

    const land_relief relief{1.25f, 0.5f, 3.0f};
    const height_grid relieved = relieved_land(land, zones, relief);
    double sum = 0.0;
    for (const float height : land.heights) {
        sum += height;
    }
    const float mean = static_cast<float>(sum / static_cast<double>(land.heights.size()));
    const auto at = [](const height_grid& grid, const int row, const int column) {
        return grid.heights[static_cast<std::size_t>(row * grid.columns + column)];
    };
    // Away from the green (the bunker changes nothing), mean + scale * (h - mean).
    for (const auto& [row, column] : std::vector<std::pair<int, int>>{{0, 0}, {10, 10}, {40, 3}, {30, 15}}) {
        CHECK(near(at(relieved, row, column), mean + 1.25f * (at(land, row, column) - mean), 0.0001f));
    }
    // On the green, its scaled mean plus half its own relief.
    double green_sum = 0.0;
    int inside = 0;
    for (int row = 25; row <= 35; ++row) {
        for (int column = 25; column <= 35; ++column) {
            if (zone_contains(green, glm::vec3(static_cast<float>(column), 0.0f, static_cast<float>(row)))) {
                green_sum += at(land, row, column);
                ++inside;
            }
        }
    }
    const float green_mean = static_cast<float>(green_sum / inside);
    for (const auto& [row, column] : std::vector<std::pair<int, int>>{{30, 30}, {28, 33}, {30, 25}}) {
        CHECK(near(at(relieved, row, column),
                   mean + 1.25f * (green_mean - mean) + 0.5f * (at(land, row, column) - green_mean), 0.0001f));
    }
    // Part way between in the blend past its edge.
    const float blended = at(relieved, 30, 23);
    const float scaled = mean + 1.25f * (at(land, 30, 23) - mean);
    const float flattened = mean + 1.25f * (green_mean - mean) + 0.5f * (at(land, 30, 23) - green_mean);
    CHECK(std::abs(blended - scaled) > 0.001f);
    CHECK(std::abs(blended - flattened) > 0.001f);
    CHECK(std::abs(blended - scaled) + std::abs(blended - flattened) < std::abs(scaled - flattened) + 0.0001f);
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
    const terrain_mesh ground = build_ground(holes, {}, nullptr, ground_settings{1.0f, 10.0f, 0.0f}).mesh;

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

TEST_CASE("a trunk pushes the ball outside and reflects it deterministically") {
    solid trunk = test_post(glm::vec3(0.0f), 0.35f, 2.4f);
    trunk.kind = solid_kind::trunk;
    trunk.restitution = 0.25f;
    trunk.friction = 0.35f;

    ball_state ball;
    ball.radius = 0.1f;
    ball.position = glm::vec3(0.30f, 1.0f, 0.0f);
    ball.velocity = glm::vec3(-2.0f, 0.0f, 0.0f);

    const ball_state r1 = resolve_solid_contact(ball, trunk);
    const ball_state r2 = resolve_solid_contact(ball, trunk);

    CHECK(r1.position.x >= trunk.radius + ball.radius - 0.0001f);
    CHECK(r1.velocity.x > 0.0f);
    CHECK(near(r1.position, r2.position));
    CHECK(near(r1.velocity, r2.velocity));
}

TEST_CASE("a cone of leaves damps and deflects the ball deterministically") {
    solid leaves = test_solid(solid_shape::cone, solid_kind::leaves, glm::vec3(0.0f, 2.0f, 0.0f), 1.5f, 3.0f);
    leaves.restitution = 0.2f;
    leaves.friction = 0.5f;

    ball_state ball;
    ball.radius = 0.1f;
    ball.position = glm::vec3(0.8f, 3.0f, 0.0f);
    ball.velocity = glm::vec3(-4.0f, 0.0f, 0.0f);

    const ball_state r1 = resolve_solid_contact(ball, leaves);
    const ball_state r2 = resolve_solid_contact(ball, leaves);

    CHECK(glm::length(r1.velocity) < glm::length(ball.velocity));
    CHECK(r1.position.x > ball.position.x);
    CHECK(near(r1.position, r2.position));
    CHECK(near(r1.velocity, r2.velocity));
}

TEST_CASE("solids leave a distant ball unchanged") {
    ball_state ball;
    ball.position = glm::vec3(5.0f, 1.0f, 0.0f);
    ball.velocity = glm::vec3(1.0f, 0.0f, 0.0f);

    for (const solid& thing : {test_post(glm::vec3(0.0f), 0.35f, 2.4f),
                               test_solid(solid_shape::cone, solid_kind::leaves, glm::vec3(0.0f, 2.4f, 0.0f), 1.6f, 3.2f),
                               test_box(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 2.0f, 1.0f)}) {
        const ball_state result = resolve_solid_contact(ball, thing);
        CHECK(near(result.position, ball.position));
        CHECK(near(result.velocity, ball.velocity));
    }
}

TEST_CASE("a box pushes a ball out of it along the side it is nearest") {
    const solid box = test_box(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 2.0f, 1.0f, 0.5f);
    ball_state ball;
    ball.radius = 0.1f;
    ball.position = glm::vec3(1.05f, 0.25f, 0.0f);
    ball.velocity = glm::vec3(-3.0f, 0.0f, 0.0f);
    const ball_state side = resolve_solid_contact(ball, box);
    CHECK(side.position.x >= 1.1f - 0.0001f);
    CHECK(side.velocity.x >= 0.0f);

    ball.position = glm::vec3(0.0f, 0.55f, 0.5f);
    ball.velocity = glm::vec3(0.0f, -3.0f, 0.0f);
    const ball_state top = resolve_solid_contact(ball, box);
    CHECK(top.position.y >= 0.6f - 0.0001f);
    CHECK(top.velocity.y >= 0.0f);
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

    const terrain_mesh ground = build_ground({mesh}, index_test_zones(), nullptr, ground_settings{8.0f, 12.0f, 0.0f}).mesh;
    CHECK(ground.spatial_index.cells_x > 0);
    CHECK(ground.spatial_index.triangle_count == static_cast<uint32_t>(ground.indices.size() / 3U));

    const terrain_mesh overlay = build_material_overlay_mesh(ground, index_test_zones());
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
}

TEST_CASE("indexed terrain sampling matches the full scan on the ground mesh") {
    const terrain_mesh mesh = index_test_mesh();
    const terrain_mesh ground = build_ground({mesh}, index_test_zones(), nullptr, ground_settings{8.0f, 12.0f, 0.0f}).mesh;
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

TEST_CASE("repeated indexed samples are deterministic") {
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
        const terrain_sample first = sample_terrain_mesh(mesh, probe, 0.0f);
        for (int repeat = 0; repeat < 16; ++repeat) {
            const terrain_sample again = sample_terrain_mesh(mesh, probe, 0.0f);
            CHECK(same_terrain_sample(again, first));
            CHECK(again.triangles_tested == first.triangles_tested);
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

TEST_CASE("the solid grid hits exactly what testing every solid hits") {
    std::vector<solid> trees;
    for (int i = 0; i < 400; ++i) {
        // A scattered wood, some trees close enough to touch each other.
        const float x = static_cast<float>((i * 37) % 200) - 100.0f + static_cast<float>(i % 7) * 0.3f;
        const float z = static_cast<float>((i * 53) % 160) - 80.0f + static_cast<float>(i % 5) * 0.4f;
        const glm::vec3 base(x, static_cast<float>(i % 3), z);
        solid trunk = test_post(base, 0.6f, 4.0f);
        solid leaves = test_solid(solid_shape::cone, solid_kind::leaves, base + glm::vec3(0.0f, 4.0f, 0.0f),
                                  4.0f + static_cast<float>(i % 4), 6.0f);
        for (solid* part : {&trunk, &leaves}) {
            part->restitution = 0.3f;
            part->friction = 0.2f;
            trees.push_back(*part);
        }
    }
    const solid_grid grid = build_solid_grid(trees, 16.0f);
    int touched = 0;
    for (int i = 0; i < 2000; ++i) {
        ball_state ball;
        ball.radius = 0.021f;
        ball.position = glm::vec3(static_cast<float>((i * 29) % 240) - 120.0f + 0.13f * static_cast<float>(i % 11),
                                  static_cast<float>(i % 12),
                                  static_cast<float>((i * 41) % 200) - 100.0f + 0.17f * static_cast<float>(i % 13));
        ball.velocity = glm::vec3(static_cast<float>(i % 9) - 4.0f, -1.0f, static_cast<float>(i % 7) - 3.0f);
        ball_state every = ball;
        for (const solid& thing : trees) {
            every = resolve_solid_contact(every, thing);
        }
        const ball_state near = resolve_ball_contacts(ball.position, ball, trees, grid, {}, 0).ball;
        CHECK(every.position == near.position);
        CHECK(every.velocity == near.velocity);
        touched += every.position != ball.position ? 1 : 0;
    }
    CHECK(touched > 50);  // the test reaches into canopies, not only empty air
    const std::vector<solid> none;
    CHECK(resolve_ball_contacts(glm::vec3(0.0f), ball_state{}, none, build_solid_grid(none, 16.0f), {}, 0).ball.position ==
          glm::vec3(0.0f));
}

TEST_CASE("a body moving over the ground stops at posts, slides along walls and is put out of boxes") {
    const std::vector<solid> post{test_post(glm::vec3(0.0f, 0.0f, 5.0f), 0.5f)};
    const ground_body walker = circle_body(0.3f, 0.0f, 0.0f);
    const auto move = [](const std::vector<solid>& solids, const ground_body& body, const glm::vec3& from, const glm::vec3& to) {
        return move_among(solids, solid_mover::walker, body, from, to);
    };

    // Straight into the post: stopped at its edge, touching it.
    const body_move stopped = move(post, walker, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, 3.0f, 10.0f));
    CHECK(stopped.touched);
    CHECK(near(stopped.position.z, 4.2f, 0.001f));
    CHECK(near(stopped.position.y, 3.0f));
    const body_move clear = move(post, walker, glm::vec3(2.0f, 0.0f, 0.0f), glm::vec3(2.0f, 0.0f, 10.0f));
    CHECK(!clear.touched);
    CHECK(near(clear.position, glm::vec3(2.0f, 0.0f, 10.0f)));

    // Against a thin net at an angle it slides along it, even moving a long
    // way in one go, never through it.
    const std::vector<solid> net{test_wall(glm::vec3(-10.0f, 0.0f, 3.0f), glm::vec3(10.0f, 0.0f, 3.0f), 0.05f)};
    const body_move slid = move(net, walker, glm::vec3(0.0f), glm::vec3(4.0f, 0.0f, 8.0f));
    CHECK(slid.touched);
    CHECK(slid.position.z < 3.0f - 0.3f);
    CHECK(slid.position.x > 3.9f);

    // A box stops what comes at it, and pushes out what starts inside it.
    const std::vector<solid> box{test_box(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 3.0f, 1.0f)};
    const body_move blocked = move(box, walker, glm::vec3(-5.0f, 0.0f, 0.0f), glm::vec3(5.0f, 0.0f, 0.0f));
    CHECK(blocked.touched);
    CHECK(near(blocked.position.x, -1.3f, 0.001f));
    const body_move out = move(box, walker, glm::vec3(0.6f, 0.0f, 0.0f), glm::vec3(0.6f, 0.0f, 0.0f));
    CHECK(near(out.position.x, 1.3f, 0.001f));

    // A box body stops with its front face at the net.
    const ground_body cart = box_body(glm::vec2(0.0f, 1.0f), 1.2f, 0.7f, 0.2f, 0.0f, 0.0f);
    const body_move parked = move({net[0], post[0]}, cart, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 2.0f));
    CHECK(parked.touched);
    CHECK(near(parked.position.z + 1.4f, 3.0f - 0.05f, 0.001f));
}

TEST_CASE("a box body is its rectangle: square corners, and boxes meet boxes by separating axes") {
    const auto move = [](const std::vector<solid>& solids, const ground_body& body, const glm::vec3& from, const glm::vec3& to) {
        return move_among(solids, solid_mover::walker, body, from, to);
    };
    // A post just inside the rectangle's corner stops its flat front.
    const ground_body cart = box_body(glm::vec2(0.0f, 1.0f), 1.2f, 0.7f, 0.0f, 0.0f, 0.0f);
    const std::vector<solid> post{test_post(glm::vec3(0.65f, 0.0f, 5.0f), 0.1f)};
    const body_move stopped = move(post, cart, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 6.0f));
    CHECK(stopped.touched);
    CHECK(near(stopped.position.z + 1.2f, 4.9f, 0.01f));
    // Beside it, it passes.
    CHECK(!move(post, box_body(glm::vec2(0.0f, 1.0f), 1.2f, 0.5f, 0.0f, 0.0f, 0.0f), glm::vec3(0.0f),
                glm::vec3(0.0f, 0.0f, 8.0f))
               .touched);

    // Turned 45 degrees, its corner leads into a wall.
    const glm::vec2 diagonal = glm::normalize(glm::vec2(1.0f, 1.0f));
    const std::vector<solid> wall{test_wall(glm::vec3(-20.0f, 0.0f, 4.0f), glm::vec3(20.0f, 0.0f, 4.0f), 0.05f)};
    const body_move cornered = move(wall, box_body(diagonal, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(0.0f),
                                    glm::vec3(0.0f, 0.0f, 4.0f));
    CHECK(cornered.touched);
    CHECK(near(cornered.position.z + std::sqrt(2.0f), 4.0f - 0.05f, 0.01f));

    // Into a box turned 30 degrees: stopped against it, never in it.
    const glm::vec3 turned(std::sin(glm::radians(30.0f)), 0.0f, std::cos(glm::radians(30.0f)));
    const std::vector<solid> block{test_box(glm::vec3(5.0f, 0.0f, 0.0f), turned, 1.0f, 1.0f)};
    const ground_body square = box_body(glm::vec2(0.0f, 1.0f), 1.0f, 1.0f, 0.0f, 0.0f, 0.0f);
    const body_move against = move(block, square, glm::vec3(0.0f), glm::vec3(5.0f, 0.0f, 0.0f));
    CHECK(against.touched);
    CHECK(against.position.x < 5.0f - 2.0f + 0.01f);
    CHECK(against.position.x > 5.0f - 1.0f - std::sqrt(2.0f) - 0.01f);
    // Moved again from there it stays put: nothing left to push.
    CHECK(near(move(block, square, against.position, against.position).position, against.position, 0.001f));
}

TEST_CASE("a walkable top no higher than a body's step-up does not stop it; higher, it does") {
    solid step = test_box(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, 1.0f), 2.0f, 2.0f, 0.25f);
    step.walkable = true;
    const glm::vec3 from(0.0f, 0.0f, 0.0f);
    const glm::vec3 to(0.0f, 0.0f, 10.0f);
    CHECK(!move_among({step}, solid_mover::walker, circle_body(0.3f, 0.0f, 0.3f), from, to).touched);
    CHECK(move_among({step}, solid_mover::walker, circle_body(0.3f, 0.0f, 0.2f), from, to).touched);
    // From up on it already, it is no step at all.
    CHECK(!move_among({step}, solid_mover::walker, circle_body(0.3f, 0.0f, 0.0f), glm::vec3(0.0f, 0.25f, 4.0f), to).touched);
    // Not walkable, as high as a step, it is a wall.
    step.walkable = false;
    CHECK(move_among({step}, solid_mover::walker, circle_body(0.3f, 0.0f, 0.3f), from, to).touched);
}

TEST_CASE("a body follows the surface within a step, falls off higher edges and lands") {
    const float gravity = 10.0f;
    // Up and down a step it stays on the surface.
    CHECK(settle_vertical(vertical_body{1.0f, 0.0f, true}, 1.2f, 0.3f, gravity, 0.02f).y == 1.2f);
    CHECK(settle_vertical(vertical_body{1.0f, 0.0f, true}, 0.8f, 0.3f, gravity, 0.02f).y == 0.8f);
    // Off an edge higher than that it falls as anything falls, and lands.
    vertical_body body{1.0f, 0.0f, true};
    float seconds = 0.0f;
    while (seconds < 2.0f) {
        body = settle_vertical(body, 0.0f, 0.3f, gravity, 0.01f);
        seconds += 0.01f;
        if (body.grounded) {
            break;
        }
    }
    CHECK(body.grounded);
    CHECK(body.y == 0.0f);
    CHECK(near(seconds, std::sqrt(2.0f * 1.0f / gravity), 0.011f));
    // A body moving up leaves the surface.
    const vertical_body rising = settle_vertical(vertical_body{0.0f, 3.0f, false}, 0.0f, 0.3f, gravity, 0.1f);
    CHECK(!rising.grounded);
    CHECK(near(rising.y, 0.3f - 0.05f, 1e-5f));
}

TEST_CASE("a body is stopped only by what stops its kind of mover") {
    solid cart_only = test_box(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 3.0f, 1.0f);
    cart_only.stops_walker = false;
    const std::vector<solid> box{cart_only};
    const ground_body body = circle_body(0.3f, 0.0f, 0.0f);
    const glm::vec3 from(-5.0f, 0.0f, 0.0f);
    const glm::vec3 to(5.0f, 0.0f, 0.0f);
    CHECK(!move_among(box, solid_mover::walker, body, from, to).touched);
    CHECK(move_among(box, solid_mover::cart, body, from, to).touched);
}

TEST_CASE("a body with friction scrapes to a stop along what it presses against; without, it slides") {
    const std::vector<solid> net{test_wall(glm::vec3(-20.0f, 0.0f, 3.0f), glm::vec3(20.0f, 0.0f, 3.0f), 0.05f)};
    const auto along = [&](const float friction) {
        const ground_body body = circle_body(0.5f, friction, 0.0f);
        return move_among(net, solid_mover::walker, body, glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(6.0f, 0.0f, 8.0f)).position.x;
    };
    CHECK(along(0.0f) > 5.9f);
    CHECK(along(0.9f) < 1.0f);
    CHECK(along(1.0f) < along(0.9f));
    CHECK(along(1.0f) > 0.0f);
}
