#include "doctest.h"

#include "game/course_session.h"
#include "game/game_state.h"
#include "game/hole_loader.h"
#include "game/play_area.h"
#include "game/progress_rules.h"
#include "game/scorecard.h"
#include "game/text_assets.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <algorithm>

#include <glm/geometric.hpp>

namespace {
string_table shipped_strings() {
    return load_text_assets(asset_root())->strings;
}

// Completes the hole being played with `strokes`.
void finish_hole(game_state& state, const int strokes) {
    state.stroke_count = strokes;
    complete_current_hole(state);
}

void walk_to(game_state& state, const glm::vec3& position) {
    state.player.position = position;
}

bool same_trees(const std::vector<tree_body>& a, const std::vector<tree_body>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const tree_body& x, const tree_body& y) {
        return x.base == y.base && x.shape.leaf_radius == y.shape.leaf_radius;
    });
}

// The cached anchors equal a fresh build for the current state.
void check_anchors_are_fresh(const game_state& state) {
    CHECK(static_anchor_cache_is_current(state));
    const static_anchor_cache fresh = build_static_anchor_cache(state);
    const static_anchor_cache& cached = state.static_anchors;
    CHECK(cached.tee_anchor == fresh.tee_anchor);
    CHECK(cached.pin_anchor == fresh.pin_anchor);
    CHECK(same_trees(cached.trees, fresh.trees));
    CHECK(cached.hub_start_markers == fresh.hub_start_markers);
    CHECK(cached.collectibles == fresh.collectibles);
}
}

TEST_CASE("a course without a world plays its holes in order") {
    game_state state = started_game(fixture_course({"test", "test2"}));
    REQUIRE(state.hole.has_value());
    CHECK(!state.hub.has_value());
    CHECK(state.hole->index == 0);
    CHECK(state.course_holes.size() == 2U);
    CHECK(can_interact_with_ball(state));

    finish_hole(state, 3);
    REQUIRE(state.hole.has_value());
    CHECK(state.hole->index == 1);
    CHECK(*state.round.strokes[0] == 3);
    CHECK(!round_finished(state.round));
    CHECK(state.stroke_count == 0);

    finish_hole(state, 4);
    CHECK(round_finished(state.round));
    CHECK(state.save.holes_completed == 2);
}

TEST_CASE("a practice hole does not complete a course") {
    course_definition practice = fixture_course({"test"});
    practice.practice = true;
    game_state state = started_game(practice);

    finish_hole(state, 2);

    CHECK(round_finished(state.round));
    CHECK(state.save.completed_course_ids.empty());
    CHECK(state.save.holes_completed == 1);
}

TEST_CASE("a course with a missing hole or world does not start") {
    game_state state = make_game_state(fixture_content(), save_data{});
    CHECK(!start_course(state, fixture_course({"test", "missing_hole"})));

    course_definition missing_world = fixture_course({"test"});
    missing_world.world = "course_worlds/missing.json";
    CHECK(!start_course(state, missing_world));
    CHECK(state.course_holes.empty());
}

TEST_CASE("a hub course starts at hole 1's start facing down hole 1") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(state.hub.has_value());
    CHECK(in_hub(state));
    CHECK(!state.hole.has_value());
    CHECK(near(horizontal(state.player.position), horizontal(state.hub->world.hole_starts[0].position), 0.001f));
    CHECK(nearby_hole_start(state) == std::optional<std::size_t>(0));
    CHECK(near(state.player.yaw, yaw_towards(state.player.position, state.hub->markers[0].pin_position), 0.0001f));
    CHECK(!can_interact_with_ball(state));
    CHECK(!ball_is_moving(state));
}

TEST_CASE("the hub terrain covers every hole start") {
    const game_state state = started_game(fixture_hub_course());
    for (const course_world_hole_start& start : state.hub->world.hole_starts) {
        const terrain_sample ground = sample_area(state.area, start.position);
        CHECK(ground.inside_surface);
    }
}

TEST_CASE("the hub apron never rises over a neighbouring hole at another height") {
    // Two parallel holes 80 m apart: a flat one, and a climbing one that
    // starts 10 m higher.
    const auto straight_hole = [](const float rise) {
        hole_data hole;
        hole.tee_position = glm::vec3(0.0f);
        hole.pin_position = glm::vec3(0.0f, rise, 200.0f);
        hole.spline.control_points = {glm::vec3(0.0f), glm::vec3(0.0f, rise * 0.5f, 100.0f), hole.pin_position};
        hole.spline.width = 20.0f;
        hole.spline.rough_width = 32.0f;
        return hole;
    };
    course_world_definition world;
    world.hole_starts.resize(2);
    world.hole_starts[0].hole_index = 0;
    world.hole_starts[1].hole_index = 1;
    world.hole_starts[1].position = glm::vec3(80.0f, 10.0f, 0.0f);
    const play_area hub = build_hub_area({straight_hole(0.0f), straight_hole(15.0f)}, world, shipped_content().tuning);

    int points_on_holes = 0;
    for (float x = -20.0f; x <= 100.0f; x += 1.7f) {
        for (float z = 0.0f; z <= 200.0f; z += 1.7f) {
            const glm::vec3 point(x, 0.0f, z);
            const terrain_sample ground = sample_area(hub, point);
            if (!ground.inside_surface) {
                continue;
            }
            ++points_on_holes;
            CHECK(sample_terrain_mesh(hub.apron, point, 0.0f).point.y < ground.point.y);
        }
    }
    CHECK(points_on_holes > 1000);
}

TEST_CASE("holes are placed by their start, rotated around the tee") {
    hole_data hole;
    hole.tee_position = glm::vec3(10.0f, 0.0f, 20.0f);
    hole.pin_position = glm::vec3(10.0f, 0.0f, 30.0f);
    course_world_hole_start start;
    start.position = glm::vec3(100.0f, 0.0f, 200.0f);
    start.rotation_degrees = 90.0f;

    CHECK(near(place_hole_point(hole, start, hole.tee_position), start.position, 0.0001f));
    CHECK(near(place_hole_point(hole, start, hole.pin_position), glm::vec3(90.0f, 0.0f, 200.0f), 0.0001f));
}

TEST_CASE("action at a hole start plays that hole where the hub shows it") {
    game_state state = started_game(fixture_hub_course());
    const hub_hole_marker marker = state.hub->markers[2];
    walk_to(state, marker.start_position);

    update_game(state, action_input(), 0.016f);

    REQUIRE(state.hole.has_value());
    CHECK(state.hole->index == 2);
    CHECK(state.round.current_hole_index == 2);
    CHECK(near(horizontal(state.hole->tee_position), horizontal(marker.tee_position), 0.001f));
    CHECK(near(horizontal(state.hole->pin_position), horizontal(marker.pin_position), 0.001f));
    CHECK(horizontal_distance(state.ball.position, marker.tee_position) < 0.5f);
    CHECK(sample_area(state.area, marker.pin_position).inside_surface);
}

TEST_CASE("completing a hub hole returns to its return position") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(start_hub_hole(state, 1));

    finish_hole(state, 4);

    CHECK(in_hub(state));
    CHECK(*state.round.strokes[1] == 4);
    CHECK(state.round.current_hole_index == 2);
    CHECK(near(horizontal(state.player.position), horizontal(state.hub->world.hole_starts[1].return_position), 0.001f));
    CHECK(state.save_requested);
}

TEST_CASE("hub holes can be played in any order and the round ends after all of them") {
    game_state state = started_game(fixture_hub_course());

    REQUIRE(start_hub_hole(state, 2));
    finish_hole(state, 5);
    CHECK(!round_finished(state.round));
    CHECK(state.save.completed_course_ids.empty());
    CHECK(!start_hub_hole(state, 2));

    REQUIRE(start_hub_hole(state, 0));
    finish_hole(state, 3);
    REQUIRE(start_hub_hole(state, 1));
    finish_hole(state, 4);

    CHECK(round_finished(state.round));
    CHECK(state.save.completed_course_ids == std::vector<std::string>{"fixture_hub"});
}

TEST_CASE("starts of played holes cannot be used again") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(start_hub_hole(state, 0));
    finish_hole(state, 3);

    walk_to(state, state.hub->world.hole_starts[0].position);
    CHECK(!nearby_hole_start(state).has_value());
}

TEST_CASE("retee does nothing in the hub") {
    game_state state = started_game(fixture_hub_course());
    const glm::vec3 before = state.player.position;

    game_input retee;
    retee.retee = true;
    update_game(state, retee, 0.016f);

    CHECK(in_hub(state));
    CHECK(state.player.position == before);
}

TEST_CASE("claiming a hub collectible gives its xp as an xp drop") {
    game_state state = started_game(fixture_hub_course());
    const course_world_collectible& lost_ball = state.hub->world.collectibles[1];
    walk_to(state, lost_ball.position);

    update_game(state, action_input(), 0.016f);

    CHECK(state.save.collected_ids == std::vector<std::string>{"lost_ball"});
    CHECK(state.save.world_flags == std::vector<std::string>{"found_lost_ball"});
    CHECK(skill_xp(state.save.skills, "fitness") >= 12);
    CHECK(!state.xp_drops.empty());
    CHECK(!nearby_collectible(state).has_value());
}

TEST_CASE("the cart is faster on a road and earns road xp only there") {
    game_state on_road = started_game(fixture_hub_course());
    const std::vector<glm::vec3>& road = on_road.hub->world.cart_roads.front().polyline;
    walk_to(on_road, road.front());
    on_road.player.yaw = yaw_towards(road[0], road[1]);

    game_state off_road = on_road;
    walk_to(off_road, glm::vec3(200.0f, 0.0f, 400.0f));
    CHECK(cart_on_road(on_road));
    CHECK(!cart_on_road(off_road));

    game_input drive;
    drive.cart_held = true;
    update_game(on_road, drive, 0.05f);
    update_game(off_road, drive, 0.05f);
    CHECK(on_road.cart.velocity > off_road.cart.velocity);

    for (game_state* state : {&on_road, &off_road}) {
        for (int i = 0; i < 80; ++i) {
            drive.action = i % 6 == 0;
            update_game(*state, drive, 0.05f);
        }
    }
    CHECK(skill_xp(on_road.save.skills, on_road.rewards.cart_on_road.skill_id) > 0);
    CHECK(skill_xp(on_road.save.skills, on_road.rewards.drift_on_road.skill_id) > 0);
    CHECK(skill_xp(off_road.save.skills, off_road.rewards.cart_on_road.skill_id) == 0);
    CHECK(skill_xp(off_road.save.skills, off_road.rewards.drift_on_road.skill_id) == 0);
}

TEST_CASE("road reach uses each road's own width") {
    game_state state = started_game(fixture_hub_course());
    course_world_cart_road wide;
    wide.width = 40.0f;
    wide.polyline = {glm::vec3(1000.0f, 0.0f, 0.0f), glm::vec3(1100.0f, 0.0f, 0.0f)};
    state.hub->world.cart_roads.push_back(wide);

    // 10 m from the narrow road: outside its reach even though another road is wide.
    walk_to(state, glm::vec3(100.0f, 0.0f, -40.0f));
    CHECK(!cart_on_road(state));
    walk_to(state, glm::vec3(1050.0f, 0.0f, 15.0f));
    CHECK(cart_on_road(state));
}

TEST_CASE("static anchors stay fresh through hub, hole and course changes") {
    game_state state = started_game(fixture_course({"test"}));
    check_anchors_are_fresh(state);

    std::uint64_t revision = state.terrain_render_revision;
    REQUIRE(start_course(state, fixture_hub_course()));
    CHECK(state.terrain_render_revision > revision);
    check_anchors_are_fresh(state);
    CHECK(state.static_anchors.trees.size() == state.area.trees.size());
    CHECK(state.static_anchors.hub_start_markers.size() == 3U);
    CHECK(state.static_anchors.collectibles.size() == 3U);
    const std::vector<tree_body> hub_trees = state.static_anchors.trees;

    revision = state.terrain_render_revision;
    walk_to(state, state.hub->world.hole_starts[1].position);
    update_game(state, action_input(), 0.016f);
    REQUIRE(state.hole.has_value());
    CHECK(state.terrain_render_revision > revision);
    check_anchors_are_fresh(state);
    CHECK(state.static_anchors.hub_start_markers.empty());

    finish_hole(state, 3);
    CHECK(in_hub(state));
    check_anchors_are_fresh(state);
    CHECK(same_trees(state.static_anchors.trees, hub_trees));
}

TEST_CASE("a replacement state keeps the terrain revision increasing") {
    game_state previous = started_hole();
    for (int i = 0; i < 5; ++i) {
        mark_terrain_render_dirty(previous);
    }
    const std::uint64_t previous_revision = previous.terrain_render_revision;

    game_state replacement = started_hole();
    CHECK(replacement.terrain_render_revision <= previous_revision);
    continue_terrain_render_revision(replacement, previous_revision);
    CHECK(replacement.terrain_render_revision == previous_revision + 1);
    check_anchors_are_fresh(replacement);

    const std::uint64_t advanced = replacement.terrain_render_revision;
    continue_terrain_render_revision(replacement, previous_revision);
    CHECK(replacement.terrain_render_revision == advanced);
}

TEST_CASE("the scorecard lists every hole and totals the played ones") {
    game_state state = started_game(fixture_course({"test", "test2", "test3"}));
    const string_table strings = shipped_strings();

    finish_hole(state, 4);
    scorecard_data card = build_scorecard_data(state, strings);
    REQUIRE(card.rows.size() == 3U);
    CHECK(card.course_name == "Fixture Course");
    CHECK(card.current_hole_index == 1U);
    CHECK(card.rows[0].hole_name == "New Hole");
    CHECK(card.rows[0].played);
    CHECK(card.rows[0].strokes == 4);
    CHECK(card.rows[0].relative_label == "+1");
    CHECK(card.rows[1].hole_name == "The Ditch");
    CHECK(!card.rows[1].played);
    CHECK(card.total_par == 3);
    CHECK(card.total_strokes == 4);

    finish_hole(state, 2);
    finish_hole(state, 3);
    card = build_scorecard_data(state, strings);
    CHECK(card.finished);
    CHECK(card.total_par == 9);
    CHECK(card.total_strokes == 9);
    CHECK(card.total_relative_label == "EVEN");
}

TEST_CASE("a new course starts with a clean scorecard") {
    game_state state = started_game(fixture_course({"test", "test2"}));
    finish_hole(state, 4);

    REQUIRE(start_course(state, fixture_course({"test3"})));
    const scorecard_data card = build_scorecard_data(state, shipped_strings());

    REQUIRE(card.rows.size() == 1U);
    CHECK(!card.rows[0].played);
    CHECK(card.total_strokes == 0);
}

TEST_CASE("relative scores read over, under and even") {
    const string_table strings = shipped_strings();
    CHECK(format_relative_score(strings, 2) == "+2");
    CHECK(format_relative_score(strings, -1) == "-1");
    CHECK(format_relative_score(strings, 0) == "EVEN");
}
