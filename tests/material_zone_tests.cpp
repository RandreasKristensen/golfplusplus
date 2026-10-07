#include "doctest.h"

#include "game/hole_loader.h"
#include "game/play_area.h"
#include "physics/material_zone.h"
#include "physics/terrain.h"
#include "test_support.h"

#include <cmath>
#include <optional>
#include <string>

#include <glm/trigonometric.hpp>

namespace {
// A green 12 m across along its own x and 4 m along its own z.
material_zone long_green(const glm::vec3& center, const float rotation_degrees) {
    material_zone zone = ellipse_zone(material_zone_type::green, center, 6.0f, 2.0f);
    zone.rotation = glm::radians(rotation_degrees);
    return zone;
}

std::optional<hole_data> hole_with_zone(const std::string& zone) {
    return parse_hole_from_text(R"({
      "tee": [0, 0, 0], "pin": [0, 0, 40],
      "spline": { "control_points": [[0, 0, 0], [0, 0, 40]], "width": 12 },
      "material_zones": [ )" + zone + R"( ]
    })");
}
}

TEST_CASE("an ellipse zone holds the points inside its own radii") {
    const material_zone zone = long_green(glm::vec3(10.0f, 3.0f, 20.0f), 0.0f);
    CHECK(zone_contains(zone, glm::vec3(15.5f, 0.0f, 20.0f)));
    CHECK(zone_contains(zone, glm::vec3(10.0f, 0.0f, 21.9f)));
    CHECK(!zone_contains(zone, glm::vec3(10.0f, 0.0f, 22.5f)));
    CHECK(!zone_contains(zone, glm::vec3(15.0f, 0.0f, 21.5f)));  // in the rectangle around it, past its edge

    // Half way out is 0.5 along either axis; the height never matters.
    CHECK(near(*zone_normalized_distance(zone, glm::vec3(13.0f, 50.0f, 20.0f)), 0.5f));
    CHECK(near(*zone_normalized_distance(zone, glm::vec3(10.0f, -50.0f, 21.0f)), 0.5f));
    CHECK(near(*zone_normalized_distance(zone, zone.center), 0.0f));
}

TEST_CASE("a circle zone is distance over radius, turned or not") {
    material_zone circle = circle_zone(material_zone_type::bunker, glm::vec3(0.0f), 4.0f);
    circle.rotation = glm::radians(37.0f);
    CHECK(near(*zone_normalized_distance(circle, glm::vec3(1.0f, 0.0f, 1.0f)), std::sqrt(2.0f) / 4.0f));
    CHECK(!zone_contains(circle, glm::vec3(3.0f, 0.0f, 3.0f)));
}

TEST_CASE("a turned ellipse zone lies along its turned axis") {
    const material_zone zone = long_green(glm::vec3(0.0f), 90.0f);
    const glm::vec3 own_x = rotate_about_y(glm::vec3(1.0f, 0.0f, 0.0f), zone.rotation);
    const glm::vec3 own_z = rotate_about_y(glm::vec3(0.0f, 0.0f, 1.0f), zone.rotation);
    CHECK(zone_contains(zone, own_x * 5.5f));
    CHECK(!zone_contains(zone, own_z * 2.5f));
    CHECK(!zone_contains(zone, glm::vec3(5.5f, 0.0f, 0.0f)));
    CHECK(zone_contains(zone, glm::vec3(0.0f, 0.0f, 5.5f)));
    CHECK(near(zone_world_point(zone, glm::vec2(6.0f, 0.0f)), own_x * 6.0f, 0.0001f));

    const glm::vec2 extent = zone_half_extent(zone);
    CHECK(near(extent.x, 2.0f, 0.0001f));
    CHECK(near(extent.y, 6.0f, 0.0001f));
    const glm::vec2 diagonal = zone_half_extent(long_green(glm::vec3(0.0f), 45.0f));
    CHECK(near(diagonal.x, std::sqrt(20.0f), 0.0001f));
    CHECK(near(diagonal.y, std::sqrt(20.0f), 0.0001f));
}

TEST_CASE("a placed hole turns its zones with it") {
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 40.0f), 12.0f);
    hole.material_zones = {long_green(glm::vec3(0.0f, 0.0f, 20.0f), 30.0f)};
    course_world_hole_start start;
    start.position = glm::vec3(100.0f, 0.0f, 50.0f);
    start.rotation_degrees = 60.0f;

    const hole_data placed = place_hole(hole, start);
    REQUIRE(placed.material_zones.size() == 1U);
    const material_zone& zone = placed.material_zones[0];
    const glm::vec3 center = place_hole_point(hole, start, glm::vec3(0.0f, 0.0f, 20.0f));
    CHECK(near(zone.center, center, 0.0001f));
    CHECK(near(zone.radii.x, 6.0f));
    CHECK(near(zone.radii.y, 2.0f));

    // 30 degrees in the hole and 60 more for the hole: a quarter turn in all.
    const glm::vec3 long_axis = rotate_about_y(glm::vec3(1.0f, 0.0f, 0.0f), glm::radians(90.0f));
    const glm::vec3 short_axis = rotate_about_y(glm::vec3(0.0f, 0.0f, 1.0f), glm::radians(90.0f));
    CHECK(zone_contains(zone, center + long_axis * 5.5f));
    CHECK(!zone_contains(zone, center + short_axis * 2.5f));
    // Every point of the hole's zone is in the placed zone where the hole puts it.
    for (const glm::vec3& local : {glm::vec3(4.0f, 0.0f, 20.5f), glm::vec3(-3.0f, 0.0f, 18.0f), glm::vec3(1.0f, 0.0f, 21.5f)}) {
        CHECK(zone_contains(hole.material_zones[0], local) == zone_contains(zone, place_hole_point(hole, start, local)));
    }
    CHECK(zone_material_at(placed.material_zones, center + long_axis * 5.5f) == terrain_material::green);
}

TEST_CASE("holes read ellipse zones") {
    const std::optional<hole_data> ellipse =
        hole_with_zone(R"({ "type": "green", "center": [1, 2, 30], "radii": [8, 5], "rotation_degrees": 45 })");
    REQUIRE(ellipse.has_value());
    REQUIRE(ellipse->material_zones.size() == 1U);
    const material_zone& green = ellipse->material_zones[0];
    CHECK(green.type == material_zone_type::green);
    CHECK(near(green.center, glm::vec3(1.0f, 2.0f, 30.0f)));
    CHECK(near(green.radii.x, 8.0f));
    CHECK(near(green.radii.y, 5.0f));
    CHECK(near(green.rotation, glm::radians(45.0f)));
}

TEST_CASE("holes with a malformed zone fail to load") {
    for (const char* zone : {
             R"({ "type": "green", "center": [0, 0, 20], "radius": 3 })",
             R"({ "type": "green", "center": [0, 0, 20], "rotation_degrees": 0 })",
             R"({ "type": "green", "center": [0, 0, 20], "radii": [3], "rotation_degrees": 0 })",
             R"({ "type": "green", "center": [0, 0, 20], "radii": [3, 0], "rotation_degrees": 0 })",
             R"({ "type": "green", "center": [0, 0, 20], "radii": [-3, 3], "rotation_degrees": 0 })",
             R"({ "type": "green", "center": [0, 0, 20], "radii": [3, "3"], "rotation_degrees": 0 })",
             R"({ "type": "green", "center": [0, 0, 20], "radii": [3, 3] })",
             R"({ "type": "green", "radii": [3, 3], "rotation_degrees": 0 })",
             R"({ "type": "water", "bounds": [[0, 0, 0], [5, 0, 9]] })",
             R"({ "type": "bunker" })",
             R"(7)"}) {
        CHECK(!hole_with_zone(zone).has_value());
    }
}
