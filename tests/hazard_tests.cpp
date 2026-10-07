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

TEST_CASE("every bunker is a bowl of the same depth, flush with the ground at its edge") {
    const glm::vec3 small(-10.0f, 0.0f, 60.0f);
    const glm::vec3 large(10.0f, 0.0f, 150.0f);
    const play_area area = flat_area({circle_zone(material_zone_type::bunker, small, 3.0f),
                                      circle_zone(material_zone_type::bunker, large, 7.0f)});
    const float depth = zone_tuning().bunker_depth;
    REQUIRE(depth > 0.0f);
    CHECK(near(carved(area, small, 3.0f), depth, 0.05f));
    CHECK(near(carved(area, large, 7.0f), depth, 0.05f));
    // A bowl: half way out it is three quarters as deep.
    CHECK(near(terrain_height(area, large + glm::vec3(8.0f, 0.0f, 0.0f)) -
                   terrain_height(area, large + glm::vec3(3.5f, 0.0f, 0.0f)),
               0.75f * depth, 0.05f));
}

TEST_CASE("a pond is as deep as it is big, within the least and most depth") {
    const terrain_zone_tuning& tuning = zone_tuning();
    const glm::vec3 pool(-12.0f, 0.0f, 60.0f);
    const glm::vec3 lake(0.0f, 0.0f, 180.0f);
    const material_zone pool_zone = circle_zone(material_zone_type::water, pool, 4.0f);
    const material_zone lake_zone = circle_zone(material_zone_type::water, lake, 25.0f);
    const play_area area = flat_area({pool_zone, lake_zone});
    CHECK(zone_depth(pool_zone, tuning) < zone_depth(lake_zone, tuning));
    CHECK(near(zone_depth(pool_zone, tuning), std::clamp(4.0f * tuning.water_depth_per_metre, tuning.water_min_depth,
                                                         tuning.water_max_depth)));
    CHECK(zone_depth(lake_zone, tuning) <= tuning.water_max_depth);
    CHECK(near(carved(area, pool, 4.0f), zone_depth(pool_zone, tuning), 0.08f));
    // (The lake reaches the hole's edge, where the ground drops by up to ribbon_edge_drop.)
    CHECK(near(carved(area, lake, 25.0f), zone_depth(lake_zone, tuning), ribbon_edge_drop + 0.05f));
    // A long thin creek is as shallow as it is narrow.
    CHECK(near(zone_depth(ellipse_zone(material_zone_type::water, lake, 40.0f, 2.0f), tuning),
               zone_depth(circle_zone(material_zone_type::water, lake, 2.0f), tuning)));
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
    CHECK(worst < 0.06f);  // a 0.1 m step along the steepest bowl moves well under this
}

TEST_CASE("a pond fills its bowl to its lowest rim, and overlapping zones are one pond") {
    const glm::vec3 a(-6.0f, 0.0f, 120.0f);
    const glm::vec3 b(6.0f, 0.0f, 120.0f);
    const play_area area = flat_area({circle_zone(material_zone_type::water, a, 9.0f),
                                      circle_zone(material_zone_type::water, b, 9.0f),
                                      circle_zone(material_zone_type::bunker, glm::vec3(0.0f, 0.0f, 40.0f), 3.0f)});
    REQUIRE(area.water_levels.size() == 3U);
    CHECK(area.water_levels[0] == area.water_levels[1]);
    const float rim = terrain_height(area, a + glm::vec3(-9.0f, 0.0f, 0.0f));
    CHECK(area.water_levels[0] <= rim + 0.001f);
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
