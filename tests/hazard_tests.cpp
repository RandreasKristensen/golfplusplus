#include "doctest.h"

#include "game/play_area.h"
#include "renderer/terrain_render_mesh.h"
#include "renderer/water_batch.h"
#include "renderer/water_light.h"

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

TEST_CASE("a lake is split finer only along its banks, its flat middle carved to full depth") {
    const terrain_zone_tuning& tuning = zone_tuning();
    const glm::vec3 centre(0.0f, 0.0f, 150.0f);
    const std::size_t dry = flat_area({}).ground.indices.size();
    const play_area small = flat_area({circle_zone(material_zone_type::water, centre, 20.0f)});
    const play_area large = flat_area({circle_zone(material_zone_type::water, centre, 40.0f)});
    // Twice the radius is twice the bank but four times the water.
    const float grown = static_cast<float>(large.ground.indices.size() - dry) / static_cast<float>(small.ground.indices.size() - dry);
    CHECK(grown < 2.6f);
    CHECK(near(large.water_levels[0] - terrain_height(large, centre), tuning.water_max_depth, 0.05f));
    CHECK(near(large.water_levels[0] - terrain_height(large, centre + glm::vec3(25.0f, 0.0f, 3.0f)), tuning.water_max_depth, 0.05f));
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

TEST_CASE("a pond fills its outline to just under its lowest rim, and overlapping zones are one pond") {
    const glm::vec3 a(-6.0f, 0.0f, 120.0f);
    const glm::vec3 b(6.0f, 0.0f, 120.0f);
    const play_area area = flat_area({circle_zone(material_zone_type::water, a, 9.0f),
                                      circle_zone(material_zone_type::water, b, 9.0f),
                                      circle_zone(material_zone_type::bunker, glm::vec3(0.0f, 0.0f, 40.0f), 3.0f)});
    REQUIRE(area.water_levels.size() == 3U);
    CHECK(area.water_levels[0] == area.water_levels[1]);
    // Under the rim by its straight bank's rise over a fine cell's diagonal.
    const float rim = terrain_height(area, a + glm::vec3(-12.0f, 0.0f, 0.0f));
    const float most_freeboard = zone_tuning().water_edge_slope * shipped_content().tuning.terrain.zone_cell_size * 1.41421356f;
    CHECK(area.water_levels[0] < rim - 0.5f * most_freeboard + 0.02f);
    CHECK(area.water_levels[0] > rim - most_freeboard - 0.02f);
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
                      [&](const render_water_vertex& v) { return near(v.position.y, area.water_levels[0]); }));
}

TEST_CASE("a pond's water meets its bank at its outline, with no dry ground below its level beside it") {
    const glm::vec3 centre(0.0f, 0.0f, 120.0f);
    const float radius = 10.0f;
    const play_area area = flat_area({circle_zone(material_zone_type::water, centre, radius)});
    REQUIRE(area.water_levels.size() == 1U);
    const float level = area.water_levels[0];
    const terrain_zone_tuning& tuning = zone_tuning();
    REQUIRE(tuning.water_shore_meters > 0.5f);
    for (float out = 0.05f; out <= tuning.water_shore_meters + 1.0f; out += 0.1f) {
        for (const glm::vec3 way : {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.6f, 0.0f, 0.8f)}) {
            CHECK(terrain_height(area, centre + way * (radius + out)) >= level - 0.01f);
        }
    }
    // Its surface lies only over ground below it and ends on the outline itself.
    const render_water water = build_render_water(area, 1);
    REQUIRE(!water.triangles.empty());
    float reach = 0.0f;
    for (const render_water_vertex& vertex : water.triangles) {
        const glm::vec3& point = vertex.position;
        CHECK(terrain_height(area, point) <= level + 0.01f);
        reach = std::max(reach, glm::length(glm::vec2(point.x - centre.x, point.z - centre.z)));
    }
    CHECK(reach > radius - 0.05f);
    CHECK(reach < radius + 0.05f);
}

TEST_CASE("a pond's fetch is nothing on its upwind shore and its width on the downwind one") {
    const glm::vec3 centre(0.0f, 0.0f, 120.0f);
    const float radius = 10.0f;
    const play_area area = flat_area({circle_zone(material_zone_type::water, centre, radius)});
    const render_water water = build_render_water(area, 1);
    REQUIRE(!water.triangles.empty());
    // Direction 0 blows towards +X: the west shore is upwind, the east downwind.
    const render_water_vertex* west = &water.triangles.front();
    const render_water_vertex* east = &water.triangles.front();
    for (const render_water_vertex& vertex : water.triangles) {
        west = vertex.position.x < west->position.x ? &vertex : west;
        east = vertex.position.x > east->position.x ? &vertex : east;
    }
    CHECK(west->fetch[0] < 2.0f);
    CHECK(east->fetch[0] > 1.6f * radius);
    // The wind turned round, the shores swap.
    CHECK(east->fetch[4] < 2.0f);
    CHECK(west->fetch[4] > 1.6f * radius);
    // Between the two kept directions, the fetch is between theirs.
    const float between = water_fetch_toward(east->fetch, glm::vec2(1.0f, 0.4142f));
    CHECK(between <= std::max(east->fetch[0], east->fetch[1]) + 0.01f);
    CHECK(between >= std::min(east->fetch[0], east->fetch[1]) - 0.01f);
}

TEST_CASE("a pond's floor is the bank's grass at its edge, mud just inside it, and water on a map") {
    const glm::vec3 centre(0.0f, 0.0f, 150.0f);
    const play_area area = flat_area({circle_zone(material_zone_type::water, centre, 20.0f)});
    const ground_colors& colors = test_scene_colors().ground;
    const render_static_mesh drawn = make_terrain_render_mesh({&area.ground}, 1U, colors, ground_light_bake{}, &area);
    REQUIRE(drawn.vertices.size() == area.ground.vertices.size());
    const auto mud = [&](const glm::vec3& color) {
        return glm::length(color - colors.rough) / glm::length(colors.pond_bed - colors.rough);
    };
    bool any_floor = false;
    for (std::size_t i = 0; i < drawn.vertices.size(); ++i) {
        const terrain_vertex& vertex = area.ground.vertices[i];
        if (vertex.material != terrain_material::water) {
            CHECK(drawn.vertices[i].color() != colors.pond_bed);
            continue;
        }
        CHECK(drawn.vertices[i].blades() == 0.0f);
        // A hand's width down the bank, the floor is all mud.
        if (area.water_levels[0] - vertex.position.y > 0.15f) {
            CHECK(mud(drawn.vertices[i].color()) > 0.99f);
        }
        any_floor = true;
    }
    CHECK(any_floor);
    // Its drawn shape keeps the bank's grass on the outline itself.
    const render_static_mesh shape = make_terrain_render_mesh({&area.material_overlay}, 1U, colors, ground_light_bake{}, &area);
    float edge_mud = 1.0f;
    for (std::size_t i = 0; i < shape.vertices.size(); ++i) {
        const glm::vec3& position = area.material_overlay.vertices[i].position;
        if (zone_edge_distance(area.zones[0], position) < 0.001f) {
            edge_mud = std::min(edge_mud, mud(shape.vertices[i].color()));
        }
    }
    // Within the colour's byte steps.
    CHECK(edge_mud < 0.05f);
    // No pond given: it is drawn in its material's colour.
    const render_static_mesh plain = make_terrain_render_mesh({&area.ground}, 1U, colors, ground_light_bake{}, nullptr);
    CHECK(std::none_of(plain.vertices.begin(), plain.vertices.end(),
                       [&](const render_terrain_vertex& vertex) { return vertex.color() == colors.pond_bed; }));
}
