#include "doctest.h"

#include "game/hole_loader.h"
#include "game/play_area.h"
#include "physics/material_zone.h"
#include "physics/terrain.h"
#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

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

TEST_CASE("holes read outline zones, each edge the curve through its mapped points") {
    const std::optional<hole_data> hole =
        hole_with_zone(R"({ "type": "fairway", "outline": [[0, 10], [6, 10], [6, 30], [0, 30]] })");
    REQUIRE(hole.has_value());
    REQUIRE(hole->material_zones.size() == 1U);
    const material_zone& fairway = hole->material_zones[0];
    CHECK(fairway.type == material_zone_type::fairway);
    CHECK(fairway.outline.size() > 4U);
    CHECK(zone_area(fairway) > 120.0f);  // rounded out past the straight sides
    CHECK(zone_contains(fairway, glm::vec3(6.5f, 0.0f, 20.0f)));
    CHECK(zone_contains(fairway, glm::vec3(3.0f, 0.0f, 20.0f)));
    CHECK(zone_material_at(hole->material_zones, glm::vec3(3.0f, 0.0f, 20.0f)) == terrain_material::fairway);
}

namespace {
// Whether any two of the closed ring's sides that don't meet cross.
bool crosses_itself(const std::vector<glm::vec2>& ring) {
    const auto side = [](const glm::vec2& p, const glm::vec2& q, const glm::vec2& r) {
        return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
    };
    const std::size_t count = ring.size();
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = i + 2U; j < count; ++j) {
            if (i == 0U && j == count - 1U) {
                continue;
            }
            const glm::vec2 &a = ring[i], &b = ring[(i + 1U) % count], &c = ring[j], &d = ring[(j + 1U) % count];
            if (side(a, b, c) * side(a, b, d) < 0.0f && side(c, d, a) * side(c, d, b) < 0.0f) {
                return true;
            }
        }
    }
    return false;
}
}

TEST_CASE("a zone's edge is a smooth curve through every mapped point, bulging round its corners") {
    const std::vector<glm::vec2> square{{0.0f, 0.0f}, {10.0f, 0.0f}, {10.0f, 10.0f}, {0.0f, 10.0f}};
    const std::vector<glm::vec2> edge = rounded_outline(square, 0.05f);
    CHECK(edge.size() > square.size());
    std::size_t next = 0;
    for (const glm::vec2& point : edge) {
        if (next < square.size() && point == square[next]) {
            ++next;
        }
    }
    CHECK(next == square.size());  // every mapped point, in order
    const material_zone zone = make_material_zone(material_zone_type::bunker, edge);
    CHECK(zone_contains(zone, glm::vec3(5.0f, 0.0f, -0.8f)));  // past the straight side's middle
    CHECK(!zone_contains(zone, glm::vec3(5.0f, 0.0f, -1.5f)));
    CHECK(!crosses_itself(edge));
    // A finer tolerance cuts the curve into more, shorter pieces.
    CHECK(rounded_outline(square, 0.01f).size() > edge.size());
}

TEST_CASE("three or more mapped points in a line keep a zone's side straight") {
    // A 30 by 10 m box, each side a run of four points, its corners rounded.
    const std::vector<glm::vec2> box{{1.0f, 0.0f},   {2.0f, 0.0f},  {28.0f, 0.0f}, {29.0f, 0.0f},  {30.0f, 1.0f},
                                     {30.0f, 2.0f},  {30.0f, 8.0f}, {30.0f, 9.0f}, {29.0f, 10.0f}, {28.0f, 10.0f},
                                     {2.0f, 10.0f},  {1.0f, 10.0f}, {0.0f, 9.0f},  {0.0f, 8.0f},   {0.0f, 2.0f},
                                     {0.0f, 1.0f}};
    const std::vector<glm::vec2> edge = rounded_outline(box, 0.05f);
    const auto at = std::find(edge.begin(), edge.end(), glm::vec2(2.0f, 0.0f));
    REQUIRE(at != edge.end());
    REQUIRE(at + 1 != edge.end());
    CHECK(*(at + 1) == glm::vec2(28.0f, 0.0f));  // nothing between: the side is one straight piece
    const material_zone zone = make_material_zone(material_zone_type::fairway, edge);
    CHECK(!zone_contains(zone, glm::vec3(15.0f, 0.0f, -0.05f)));
    CHECK(zone_area(zone) < 302.0f);
}

TEST_CASE("a zone's neck narrower than its curve's bulge stays straight rather than crossing itself") {
    // Two 10 m squares joined by a neck 0.4 m wide and 2 m long.
    const std::vector<glm::vec2> neck{{0.0f, 0.0f},  {10.0f, 0.0f},  {10.0f, 4.8f},  {12.0f, 4.8f},
                                      {12.0f, 0.0f}, {22.0f, 0.0f},  {22.0f, 10.0f}, {12.0f, 10.0f},
                                      {12.0f, 5.2f}, {10.0f, 5.2f},  {10.0f, 10.0f}, {0.0f, 10.0f}};
    const std::vector<glm::vec2> edge = rounded_outline(neck, 0.05f);
    CHECK(edge.size() > neck.size());
    CHECK(!crosses_itself(edge));
    CHECK(zone_contains(make_material_zone(material_zone_type::fairway, edge), glm::vec3(11.0f, 0.0f, 5.0f)));
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
    REQUIRE(!ribbon_paints_fairway(hole));
    const play_area outlined = hole_area(hole, shipped_content().tuning);
    CHECK(sample_area(outlined, glm::vec3(0.0f, 0.0f, 40.0f)).material == terrain_material::rough);
    CHECK(sample_area(outlined, glm::vec3(0.0f, 0.0f, 150.0f)).material == terrain_material::fairway);
    CHECK(sample_area(outlined, glm::vec3(13.0f, 0.0f, 280.0f)).material == terrain_material::rough);
}
