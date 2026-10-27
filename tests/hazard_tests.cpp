#include "doctest.h"

#include "game/play_area.h"
#include "renderer/water_batch.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>

namespace {
// A long flat hole with `zones` on it, as the shipped tuning builds it.
play_area flat_area(const std::vector<material_zone>& zones) {
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 300.0f), 60.0f);
    hole.material_zones = zones;
    return hole_area(hole, shipped_content().tuning);
}

const terrain_zone_tuning& zone_tuning() {
    return shipped_content().tuning.terrain.zones;
}

// How far the ground at the zone's centre lies below the ground just outside
// its edge (at the edge itself the finer cells round the lip a little).
float carved(const play_area& area, const glm::vec3& centre, const float radius) {
    return terrain_height(area, centre + glm::vec3(radius + 1.0f, 0.0f, 0.0f)) - terrain_height(area, centre);
}
}

TEST_CASE("every bunker has one flat floor, its lip easing down over the lip's width") {
    const glm::vec3 small(-10.0f, 0.0f, 60.0f);
    const glm::vec3 large(10.0f, 0.0f, 150.0f);
    const play_area area = flat_area({circle_zone(material_zone_type::bunker, small, 3.0f),
                                      circle_zone(material_zone_type::bunker, large, 7.0f)});
    const terrain_zone_tuning& tuning = zone_tuning();
    REQUIRE(tuning.bunker_depth > 0.0f);
    REQUIRE(tuning.bunker_lip_meters < 3.0f);
    CHECK(near(carved(area, small, 3.0f), tuning.bunker_depth, 0.05f));
    CHECK(near(carved(area, large, 7.0f), tuning.bunker_depth, 0.05f));
    CHECK(near(terrain_height(area, large + glm::vec3(3.5f, 0.0f, 0.0f)), terrain_height(area, large), 0.02f));
    // Half the lip in, half as deep.
    CHECK(near(terrain_height(area, large + glm::vec3(8.0f, 0.0f, 0.0f)) -
                   terrain_height(area, large + glm::vec3(7.0f - 0.5f * tuning.bunker_lip_meters, 0.0f, 0.0f)),
               0.5f * tuning.bunker_depth, 0.1f));
}

TEST_CASE("overlapping bunkers carve one floor with no ridge between them") {
    const glm::vec3 centre(0.0f, 0.0f, 100.0f);
    const play_area area = flat_area({circle_zone(material_zone_type::bunker, centre - glm::vec3(4.0f, 0.0f, 0.0f), 5.0f),
                                      circle_zone(material_zone_type::bunker, centre + glm::vec3(4.0f, 0.0f, 0.0f), 5.0f)});
    const float floor = terrain_height(area, centre + glm::vec3(20.0f, 0.0f, 0.0f)) - zone_tuning().bunker_depth;
    for (float x = -6.0f; x <= 6.0f; x += 0.25f) {
        CHECK(near(terrain_height(area, centre + glm::vec3(x, 0.0f, 0.0f)), floor, 0.05f));
    }
}

TEST_CASE("a pond is shallow at its edge and deepest past its bank") {
    const terrain_zone_tuning& tuning = zone_tuning();
    const glm::vec3 pool(-12.0f, 0.0f, 60.0f);
    const glm::vec3 lake(0.0f, 0.0f, 180.0f);
    REQUIRE(near(tuning.water_bank_meters, 4.0f));
    const play_area area = flat_area({circle_zone(material_zone_type::water, pool, 2.0f),
                                      circle_zone(material_zone_type::water, lake, 20.0f)});
    REQUIRE(area.water_levels.size() == 2U);
    CHECK(near(area.water_levels[1] - terrain_height(area, lake), tuning.water_max_depth, 0.05f));
    CHECK(near(area.water_levels[1] - terrain_height(area, lake + glm::vec3(0.0f, 0.0f, 10.0f)), tuning.water_max_depth, 0.05f));
    // Half the bank in, half way between the least and most depth.
    CHECK(near(area.water_levels[0] - terrain_height(area, pool),
               0.5f * (tuning.water_min_depth + tuning.water_max_depth), 0.15f));
}

TEST_CASE("the ground has no step where a bowl's finer cells meet the rest") {
    const glm::vec3 centre(0.0f, 0.0f, 100.0f);
    const play_area area = flat_area({circle_zone(material_zone_type::bunker, centre, 5.0f)});
    float previous = terrain_height(area, centre - glm::vec3(20.0f, 0.0f, 0.0f));
    float worst = 0.0f;
    for (float x = -20.0f; x <= 20.0f; x += 0.1f) {
        const float height = terrain_height(area, centre + glm::vec3(x, 0.0f, 0.37f));
        worst = std::max(worst, std::abs(height - previous));
        previous = height;
    }
    // A 0.1 m step down the steepest of the lip (a smoothstep: 1.5 times its mean slope).
    const terrain_zone_tuning& tuning = zone_tuning();
    CHECK(worst < 0.1f * 1.5f * tuning.bunker_depth / tuning.bunker_lip_meters + 0.02f);
}

TEST_CASE("a pond fills its outline to its lowest rim, and overlapping zones are one pond") {
    const glm::vec3 a(-6.0f, 0.0f, 120.0f);
    const glm::vec3 b(6.0f, 0.0f, 120.0f);
    const play_area area = flat_area({circle_zone(material_zone_type::water, a, 9.0f),
                                      circle_zone(material_zone_type::water, b, 9.0f),
                                      circle_zone(material_zone_type::bunker, glm::vec3(0.0f, 0.0f, 40.0f), 3.0f)});
    REQUIRE(area.water_levels.size() == 3U);
    CHECK(area.water_levels[0] == area.water_levels[1]);
    CHECK(near(area.water_levels[0], terrain_height(area, a + glm::vec3(-12.0f, 0.0f, 0.0f)), 0.02f));
    const terrain_sample middle = sample_area(area, glm::vec3(0.0f, 0.0f, 120.0f));
    REQUIRE(middle.material == terrain_material::water);
    CHECK(near(middle.water_level, area.water_levels[0]));
    CHECK(middle.point.y < middle.water_level - 1.0f);

    CHECK(under_water(area, glm::vec3(a.x, middle.water_level - 0.5f, a.z)));
    CHECK(!under_water(area, glm::vec3(a.x, middle.water_level + 0.5f, a.z)));
    CHECK(!under_water(area, glm::vec3(0.0f, -5.0f, 40.0f)));  // a bunker is not water

    // Its surface is drawn flat at that level.
    const render_water water = build_render_water(area, 3);
    REQUIRE(!water.triangles.empty());
    CHECK(water.triangles.size() % 3U == 0U);
    CHECK(std::all_of(water.triangles.begin(), water.triangles.end(),
                      [&](const glm::vec3& p) { return near(p.y, area.water_levels[0]); }));
}
