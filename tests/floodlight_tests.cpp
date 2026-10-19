#include "doctest.h"

#include "game/floodlights.h"
#include "game/hole_sign.h"
#include "game/play_area.h"
#include "game/shot_simulation.h"
#include "physics/vector_math.h"
#include "renderer/floodlight_batch.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/geometric.hpp>

namespace {
const game_tuning& tuning() {
    return shipped_content().tuning;
}

// A straight hole from the origin down +z, `length` long, its green round the pin.
hole_data long_hole(const float length) {
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, length), 30.0f);
    hole.spline.rough_width = 90.0f;
    material_zone green;
    green.type = material_zone_type::green;
    green.center = hole.pin_position;
    green.radii = glm::vec2(12.0f, 12.0f);
    hole.material_zones = {green};
    return hole;
}

std::vector<floodlight> of_kind(const std::vector<floodlight>& lights, const floodlight_target kind) {
    std::vector<floodlight> out;
    std::copy_if(lights.begin(), lights.end(), std::back_inserter(out),
                 [kind](const floodlight& light) { return light.target == kind; });
    return out;
}

int expected_fairway_poles(const float length) {
    const floodlight_tuning& rules = tuning().floodlights;
    const float stretch = length - rules.tee_clearance - rules.green_clearance;
    return stretch < 0.0f ? 0 : 1 + static_cast<int>(std::floor(stretch / rules.spacing));
}

ball_state ball_at(const glm::vec3& position, const glm::vec3& velocity) {
    ball_state ball;
    ball.radius = tuning().scale.ball_physics_radius_meters;
    ball.position = position;
    ball.velocity = velocity;
    return ball;
}
}

TEST_CASE("floodlights stand down the fairway on alternating sides, one at the tee and one each side of the green") {
    const float length = 400.0f;
    const hole_data hole = long_hole(length);
    const play_area area = hole_area(hole, tuning());
    const floodlight_tuning& rules = tuning().floodlights;

    const std::vector<floodlight> fairway = of_kind(area.floodlights, floodlight_target::fairway);
    REQUIRE(static_cast<int>(fairway.size()) == expected_fairway_poles(length));
    REQUIRE(fairway.size() >= 4U);
    CHECK(of_kind(area.floodlights, floodlight_target::tee).size() == 1U);
    CHECK(of_kind(area.floodlights, floodlight_target::green).size() == 2U);

    // The hole runs down +z, so its right is -x: right, left, right...
    for (std::size_t i = 0; i < fairway.size(); ++i) {
        const bool right = i % 2 == 0;
        CHECK((fairway[i].foot.x < 0.0f) == right);
        CHECK(std::abs(fairway[i].foot.x) >= hole.spline.width * 0.5f + rules.fairway_offset - 0.001f);
        if (i > 0) {
            CHECK(near(fairway[i].foot.z - fairway[i - 1].foot.z, rules.spacing, 0.01f));
        }
        CHECK(fairway[i].foot.z >= rules.tee_clearance - 0.001f);
        CHECK(fairway[i].foot.z <= length - rules.green_clearance + 0.001f);
    }
    // The tee's on the left of the box (its sign is on the right), the green's either side.
    CHECK(of_kind(area.floodlights, floodlight_target::tee).front().foot.x > area.tee_boxes.front().half_width);
    const std::vector<floodlight> green = of_kind(area.floodlights, floodlight_target::green);
    CHECK(green[0].foot.x * green[1].foot.x < 0.0f);

    for (const floodlight& light : area.floodlights) {
        // In the rough: never on the fairway, green, tee box, sand or water.
        CHECK(sample_area(area, light.foot).material == terrain_material::rough);
        CHECK(!on_tee_box(area.tee_boxes.front(), light.foot));
        CHECK(near(light.foot.y, terrain_height(area, light.foot), 0.001f));
        // The lamp up top, aimed down and in at what it lights.
        CHECK(near(light.lamp.y - light.foot.y, rules.pole_height, 0.001f));
        CHECK(near(glm::length(light.aim), 1.0f, 0.0001f));
        CHECK(light.aim.y < 0.0f);
        const glm::vec3 lit = light.target == floodlight_target::green ? hole.pin_position
            : light.target == floodlight_target::tee ? area.tee_boxes.front().center
            : glm::vec3(0.0f, 0.0f, light.foot.z);
        CHECK(glm::dot(horizontal(light.aim), horizontal(lit - light.foot)) > 0.0f);
    }

    // The same every time: the server stands the same poles.
    const play_area again = hole_area(hole, tuning());
    REQUIRE(again.floodlights.size() == area.floodlights.size());
    for (std::size_t i = 0; i < area.floodlights.size(); ++i) {
        CHECK(again.floodlights[i].foot == area.floodlights[i].foot);
        CHECK(again.floodlights[i].lamp == area.floodlights[i].lamp);
        CHECK(again.floodlights[i].aim == area.floodlights[i].aim);
    }

    // Each pole is a leafless trunk up to its lamp.
    REQUIRE(area.floodlight_poles.size() == area.floodlights.size());
    const tree_body& pole = area.floodlight_poles.front();
    CHECK(near(pole.base, area.floodlights.front().foot, 0.0001f));
    CHECK(near(pole.shape.trunk_radius, rules.pole_radius, 0.0001f));
    CHECK(near(pole.shape.trunk_height, rules.pole_height, 0.001f));
    CHECK(pole.shape.leaf_radius == 0.0f);
    CHECK(pole.shape.leaf_height == 0.0f);
}

TEST_CASE("a short par 3 has fewer floodlights, its tee and green still lit") {
    const play_area long_area = hole_area(long_hole(400.0f), tuning());
    const play_area short_area = hole_area(long_hole(110.0f), tuning());
    CHECK(of_kind(short_area.floodlights, floodlight_target::fairway).size() <= 1U);
    CHECK(short_area.floodlights.size() < long_area.floodlights.size());
    CHECK(of_kind(short_area.floodlights, floodlight_target::tee).size() == 1U);
    CHECK(of_kind(short_area.floodlights, floodlight_target::green).size() == 2U);
}

TEST_CASE("a floodlight stands clear of trees and off the cart road, or not at all") {
    const hole_data open = long_hole(400.0f);
    const play_area clear = hole_area(open, tuning());
    const floodlight first = of_kind(clear.floodlights, floodlight_target::fairway).front();
    const floodlight_tuning& rules = tuning().floodlights;

    hole_data wooded = open;
    tree_instance tree;
    tree.position = first.foot;
    tree.shape = tree_shape{0.4f, 3.0f, 2.5f, 5.0f};
    wooded.trees = {tree};
    const play_area area = hole_area(wooded, tuning());
    for (const floodlight& light : area.floodlights) {
        CHECK(horizontal_distance(light.foot, tree.position) >=
              tree.shape.trunk_radius + rules.pole_radius + rules.obstacle_clearance - 0.001f);
    }

    course_world_definition world = hole_world(open);
    course_world_cart_road road;
    road.width = 3.0f;
    road.polyline = {first.foot - glm::vec3(0.0f, 0.0f, 50.0f), first.foot + glm::vec3(0.0f, 0.0f, 50.0f)};
    world.cart_roads = {road};
    const play_area beside_road = build_course_area({open}, world, tuning());
    for (const floodlight& light : beside_road.floodlights) {
        CHECK(!on_cart_road(world.cart_roads, light.foot, tuning().cart));
    }
}

TEST_CASE("a shot into a floodlight pole is stopped like one into a tree trunk") {
    const hole_data hole = long_hole(400.0f);
    const play_area area = hole_area(hole, tuning());
    const auto first_fairway = std::find_if(area.floodlights.begin(), area.floodlights.end(), [](const floodlight& light) {
        return light.target == floodlight_target::fairway;
    });
    REQUIRE(first_fairway != area.floodlights.end());
    const floodlight& pole = *first_fairway;
    const tree_body& pole_body = area.floodlight_poles[static_cast<std::size_t>(first_fairway - area.floodlights.begin())];
    const club_stats& stats = shipped_content().clubs.front().stats;
    const std::vector<tree_body> no_trees;
    const shot_course course{area, no_trees, shot_hole{hole.pin_position, hole.wind_seed}};

    // Low from the fairway straight at the pole, 10 m off.
    const glm::vec3 from_fairway = safe_normalize(glm::vec3(-pole.foot.x, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 start = pole.foot + from_fairway * 10.0f + world_up * 2.5f;
    const ball_state launched = ball_at(start, -from_fairway * 20.0f);
    const shot_result hit = simulate_ball(launched, stats, course, tuning(), 0.0f);
    CHECK(std::any_of(hit.events.begin(), hit.events.end(),
                      [](const shot_event& event) { return event.kind == shot_event_kind::tree_hit; }));
    // It never got past the pole.
    CHECK(glm::dot(hit.rest_position - pole.foot, from_fairway) > 0.0f);

    // The same shot with the pole taken away and a trunk of its shape in its
    // place comes to rest at the same spot, at the same moment.
    play_area without = area;
    without.floodlights.clear();
    without.floodlight_poles.clear();
    without.floodlight_grid = tree_grid{};
    const std::vector<tree_body> trunk = {pole_body};
    const shot_course as_tree{without, trunk, shot_hole{hole.pin_position, hole.wind_seed}};
    const shot_result tree_hit = simulate_ball(launched, stats, as_tree, tuning(), 0.0f);
    CHECK(tree_hit.rest_position == hit.rest_position);
    CHECK(tree_hit.duration == hit.duration);
}

TEST_CASE("floodlights are drawn as a pole, an arm and a lamp head whose face glows") {
    const play_area area = hole_area(long_hole(400.0f), tuning());
    const render_floodlights drawn = build_render_floodlights(area, 3U, test_scene_colors().fixtures);
    CHECK(drawn.revision == 3U);
    REQUIRE(!drawn.vertices.empty());
    CHECK(drawn.vertices.size() % 3U == 0U);
    CHECK(drawn.vertices.size() % area.floodlights.size() == 0U);
    const auto glowing = std::count_if(drawn.vertices.begin(), drawn.vertices.end(),
                                       [](const floodlight_vertex& vertex) { return vertex.lamp > 0.0f; });
    // Two faces of two triangles each per lamp: the lens and the rim under it.
    CHECK(static_cast<std::size_t>(glowing) == area.floodlights.size() * 12U);
    float top = 0.0f;
    for (const floodlight_vertex& vertex : drawn.vertices) {
        CHECK(near(glm::length(vertex.normal), 1.0f, 0.001f));
        top = std::max(top, vertex.position.y);
    }
    CHECK(top > tuning().floodlights.pole_height);
}
