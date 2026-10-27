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
// An L: a 10 m square with its top right 5 m square cut away.
material_zone l_shape(const material_zone_type type) {
    return make_material_zone(type, {{0.0f, 0.0f}, {10.0f, 0.0f}, {10.0f, 5.0f}, {5.0f, 5.0f}, {5.0f, 10.0f}, {0.0f, 10.0f}});
}

std::optional<hole_data> hole_with_zone(const std::string& zone) {
    return parse_hole_from_text(R"({
      "tee": [0, 0, 0], "pin": [0, 0, 40],
      "spline": { "control_points": [[0, 0, 0], [0, 0, 40]], "width": 12 },
      "material_zones": [ )" + zone + R"( ]
    })");
}
}

TEST_CASE("an outline zone holds the points inside its edge, whatever their height") {
    const material_zone zone = l_shape(material_zone_type::green);
    CHECK(zone_contains(zone, glm::vec3(2.0f, 50.0f, 2.0f)));
    CHECK(zone_contains(zone, glm::vec3(8.0f, -50.0f, 2.0f)));
    CHECK(zone_contains(zone, glm::vec3(2.0f, 0.0f, 8.0f)));
    CHECK(!zone_contains(zone, glm::vec3(8.0f, 0.0f, 8.0f)));  // in its box, in the cut-away corner
    CHECK(!zone_contains(zone, glm::vec3(-1.0f, 0.0f, 2.0f)));
    CHECK(near(zone.center, glm::vec3(5.0f, 0.0f, 5.0f)));
    CHECK(near(zone_half_extent(zone).x, 5.0f));
    CHECK(near(zone_area(zone), 75.0f));
}

TEST_CASE("a zone's depth and distance are metres from its edge") {
    const material_zone zone = l_shape(material_zone_type::bunker);
    CHECK(near(*zone_depth_inside(zone, glm::vec3(2.0f, 0.0f, 2.0f)), 2.0f));
    CHECK(near(*zone_depth_inside(zone, glm::vec3(4.5f, 0.0f, 7.0f)), 0.5f));  // the inner side is nearest
    CHECK(!zone_depth_inside(zone, glm::vec3(8.0f, 0.0f, 8.0f)));
    CHECK(zone_distance_outside(zone, glm::vec3(4.5f, 0.0f, 7.0f)) == 0.0f);
    CHECK(near(zone_distance_outside(zone, glm::vec3(8.0f, 20.0f, 8.0f)), 3.0f));
    CHECK(near(zone_distance_outside(zone, glm::vec3(13.0f, 0.0f, 3.0f)), 3.0f));
}

TEST_CASE("a zone's triangles cover its outline exactly") {
    for (const material_zone& zone : {l_shape(material_zone_type::green),
                                      ellipse_zone(material_zone_type::water, glm::vec3(3.0f, 0.0f, -2.0f), 9.0f, 4.0f)}) {
        float area = 0.0f;
        for (const auto& [a, b, c] : zone_triangles(zone)) {
            area += std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) * 0.5f;
            const glm::vec2 middle = (a + b + c) / 3.0f;
            CHECK(zone_contains(zone, glm::vec3(middle.x, 0.0f, middle.y)));
        }
        CHECK(near(area, zone_area(zone), 0.01f));
    }
}

TEST_CASE("the edge of overlapping zones is only where it meets ground outside them all") {
    const material_zone left = make_material_zone(material_zone_type::bunker, {{0.0f, 0.0f}, {6.0f, 0.0f}, {6.0f, 4.0f}, {0.0f, 4.0f}});
    const material_zone right = make_material_zone(material_zone_type::bunker, {{4.0f, -1.0f}, {10.0f, -1.0f}, {10.0f, 5.0f}, {4.0f, 5.0f}});
    const std::vector<zone_edge> edge = edge_outside(left, {&left, &right});
    // The right side at x 6 is inside the other; the top and bottom stop at x 4.
    const glm::vec3 inside_both(5.0f, 0.0f, 2.0f);
    CHECK(near(edges_distance(edge, inside_both), std::sqrt(5.0f)));
    CHECK(near(edges_distance(edge_outside(left, {&left}), inside_both), 1.0f));
    float length = 0.0f;
    for (const zone_edge& piece : edge) {
        length += glm::length(piece[1] - piece[0]);
    }
    CHECK(near(length, 4.0f + 4.0f + 4.0f));
}

TEST_CASE("a placed hole moves and turns its zones with it") {
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 40.0f), 12.0f);
    hole.material_zones = {l_shape(material_zone_type::green)};
    course_world_hole_start start;
    start.position = glm::vec3(100.0f, 0.0f, 50.0f);
    start.rotation_degrees = 60.0f;

    const hole_data placed = place_hole(hole, start);
    REQUIRE(placed.material_zones.size() == 1U);
    const material_zone& zone = placed.material_zones[0];
    CHECK(near(zone_area(zone), 75.0f, 0.01f));
    // Every point of the hole's zone is in the placed zone where the hole puts it.
    for (const glm::vec3& local : {glm::vec3(2.0f, 0.0f, 2.0f), glm::vec3(8.0f, 0.0f, 8.0f), glm::vec3(2.0f, 0.0f, 9.0f),
                                   glm::vec3(9.0f, 0.0f, 4.0f), glm::vec3(-1.0f, 0.0f, 5.0f)}) {
        CHECK(zone_contains(hole.material_zones[0], local) == zone_contains(zone, place_hole_point(hole, start, local)));
    }
    CHECK(zone_material_at(placed.material_zones, place_hole_point(hole, start, glm::vec3(2.0f, 0.0f, 8.0f))) ==
          terrain_material::green);
}

TEST_CASE("holes read outline zones") {
    const std::optional<hole_data> hole =
        hole_with_zone(R"({ "type": "fairway", "outline": [[0, 10], [6, 10], [6, 30], [0, 30]] })");
    REQUIRE(hole.has_value());
    REQUIRE(hole->material_zones.size() == 1U);
    const material_zone& fairway = hole->material_zones[0];
    CHECK(fairway.type == material_zone_type::fairway);
    CHECK(near(zone_area(fairway), 120.0f));
    CHECK(zone_contains(fairway, glm::vec3(3.0f, 0.0f, 20.0f)));
    CHECK(zone_material_at(hole->material_zones, glm::vec3(3.0f, 0.0f, 20.0f)) == terrain_material::fairway);
}

TEST_CASE("holes with a malformed zone fail to load") {
    for (const char* zone : {
             R"({ "type": "green", "center": [0, 0, 20], "radii": [3, 3], "rotation_degrees": 0 })",
             R"({ "type": "green", "outline": [[0, 0], [5, 0]] })",
             R"({ "type": "green", "outline": [[0, 0], [5, 0], [10, 0]] })",
             R"({ "type": "green", "outline": [[0, 0], [5, 0], [5]] })",
             R"({ "type": "green", "outline": [[0, 0], [5, 0], [5, "5"]] })",
             R"({ "type": "green", "outline": 7 })",
             R"({ "type": "bunker" })",
             R"(7)"}) {
        CHECK(!hole_with_zone(zone).has_value());
    }
}

TEST_CASE("a hole with a fairway outline has fairway only inside it") {
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 300.0f), 30.0f);
    const play_area ribbon = hole_area(hole, shipped_content().tuning);
    CHECK(sample_area(ribbon, glm::vec3(0.0f, 0.0f, 40.0f)).material == terrain_material::fairway);

    // A fairway starting 100 m out, rough after the tee.
    hole.material_zones = {make_material_zone(material_zone_type::fairway, {{-15.0f, 100.0f}, {15.0f, 100.0f}, {12.0f, 290.0f}, {-12.0f, 290.0f}})};
    REQUIRE(has_fairway_outline(hole));
    const play_area outlined = hole_area(hole, shipped_content().tuning);
    CHECK(sample_area(outlined, glm::vec3(0.0f, 0.0f, 40.0f)).material == terrain_material::rough);
    CHECK(sample_area(outlined, glm::vec3(0.0f, 0.0f, 150.0f)).material == terrain_material::fairway);
    CHECK(sample_area(outlined, glm::vec3(13.0f, 0.0f, 280.0f)).material == terrain_material::rough);
}
