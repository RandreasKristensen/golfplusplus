#include "doctest.h"

#include "game/area_surface.h"
#include "game/play_area.h"

#include "test_support.h"

#include <cstddef>
#include <vector>

#include <glm/vec3.hpp>

namespace {
// Two holes up +Z, 100 apart, rough beside the fairway. The first has a green
// at its pin and a bunker on its fairway, the second a pond beside its fairway
// and a second green lying over the first hole's rough.
play_area two_holes() {
    hole_data first = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 200.0f), 20.0f);
    first.spline.rough_width = 40.0f;
    first.material_zones = {circle_zone(material_zone_type::green, first.pin_position, 12.0f),
                            circle_zone(material_zone_type::bunker, glm::vec3(0.0f, 0.0f, 100.0f), 5.0f)};
    hole_data second = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 200.0f), 20.0f);
    second.spline.rough_width = 40.0f;
    second.material_zones = {circle_zone(material_zone_type::water, glm::vec3(-16.0f, 0.0f, 50.0f), 4.0f),
                             circle_zone(material_zone_type::green, second.pin_position, 12.0f),
                             circle_zone(material_zone_type::green, glm::vec3(-85.0f, 0.0f, 150.0f), 3.0f)};
    course_world_definition world;
    world.hole_starts.resize(2);
    world.hole_starts[0].hole_index = 0;
    world.hole_starts[1].hole_index = 1;
    world.hole_starts[1].position = glm::vec3(100.0f, 0.0f, 0.0f);
    return build_course_area({first, second}, world, shipped_content().tuning);
}
}

TEST_CASE("the surface at a point is its zone's, else its hole's fairway or rough, else rough") {
    const play_area area = two_holes();
    const auto material = [&](const float x, const float z) { return surface_at(area, glm::vec3(x, 0.0f, z)).material; };
    CHECK(material(0.0f, 200.0f) == terrain_material::green);
    CHECK(material(0.0f, 100.0f) == terrain_material::bunker);  // a zone wins over the fairway under it
    CHECK(material(0.0f, 50.0f) == terrain_material::fairway);
    CHECK(material(15.0f, 50.0f) == terrain_material::rough);   // on the ribbon, past its fairway
    CHECK(material(50.0f, 50.0f) == terrain_material::rough);   // off every hole
    CHECK(material(100.0f, 200.0f) == terrain_material::green);
    CHECK(material(84.0f, 50.0f) == terrain_material::water);
    CHECK(material(100.0f, 50.0f) == terrain_material::fairway);

    const area_surface off = surface_at(area, glm::vec3(50.0f, 0.0f, 50.0f));
    CHECK(!off.zone.has_value());
    CHECK(!off.on_hole.has_value());
    const area_surface on_ribbon = surface_at(area, glm::vec3(15.0f, 0.0f, 50.0f));
    CHECK(!on_ribbon.zone.has_value());
    CHECK(on_ribbon.on_hole.has_value());
    const area_surface in_zone = surface_at(area, glm::vec3(0.0f, 0.0f, 200.0f));
    REQUIRE(in_zone.zone.has_value());
    CHECK(area.zones[in_zone.zone->index].type == material_zone_type::green);
}

TEST_CASE("the ball, walking and driving play the surface the maps show") {
    const play_area area = two_holes();
    for (float x = -30.0f; x <= 130.0f; x += 3.5f) {
        for (float z = -20.0f; z <= 230.0f; z += 4.5f) {
            const glm::vec3 at(x, 0.0f, z);
            CHECK(sample_area(area, at).material == surface_at(area, at).material);
        }
    }
}

TEST_CASE("a zone belongs to the hole it was placed with") {
    const play_area area = two_holes();
    REQUIRE(area.zone_holes.size() == area.zones.size());
    REQUIRE(area.zones.size() == 5U);
    CHECK((area.zone_holes[0] == 0U && area.zone_holes[1] == 0U));
    CHECK((area.zone_holes[2] == 1U && area.zone_holes[3] == 1U && area.zone_holes[4] == 1U));
}

TEST_CASE("a hole's map owns its own zones and what lies on its ribbon, never another hole's off it; bunkers and water are every hole's") {
    const play_area area = two_holes();
    const auto owns = [&](const std::size_t hole, const float x, const float z) {
        const glm::vec3 at(x, 0.0f, z);
        return hole_owns_surface(area, surface_at(area, at), hole, at);
    };
    // Its own green and its own ribbon.
    CHECK(owns(0, 0.0f, 200.0f));
    CHECK(!owns(1, 0.0f, 200.0f));
    CHECK(owns(1, 100.0f, 200.0f));
    CHECK(owns(0, 0.0f, 50.0f));
    CHECK(!owns(1, 0.0f, 50.0f));
    CHECK(owns(0, 15.0f, 50.0f));
    CHECK(!owns(1, 15.0f, 50.0f));
    // Another hole's green lying over its rough is on both holes' maps.
    CHECK(surface_at(area, glm::vec3(15.0f, 0.0f, 150.0f)).material == terrain_material::green);
    CHECK((owns(0, 15.0f, 150.0f) && owns(1, 15.0f, 150.0f)));
    // Off every hole nobody's.
    CHECK(!owns(0, 50.0f, 50.0f));
    CHECK(!owns(1, 50.0f, 50.0f));
    // The bunker and the pond are on every hole's map.
    CHECK((owns(0, 0.0f, 100.0f) && owns(1, 0.0f, 100.0f)));
    CHECK((owns(0, 84.0f, 50.0f) && owns(1, 84.0f, 50.0f)));
    // A zone no hole owns is every hole's.
    play_area unowned = area;
    unowned.zone_holes.clear();
    const glm::vec3 green(0.0f, 0.0f, 200.0f);
    CHECK(hole_owns_surface(unowned, surface_at(unowned, green), 1, green));
}

TEST_CASE("ground is where the ground mesh is, and nowhere else") {
    const play_area area = two_holes();
    CHECK(ground_at(area, glm::vec3(0.0f, 0.0f, 100.0f)));
    CHECK(ground_at(area, glm::vec3(50.0f, 0.0f, 50.0f)));
    CHECK(!ground_at(area, glm::vec3(5000.0f, 0.0f, 50.0f)));
    CHECK(!ground_at(area, glm::vec3(0.0f, 0.0f, -5000.0f)));
}
