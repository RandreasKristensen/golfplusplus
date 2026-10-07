#include "doctest.h"

#include "game/course_session.h"
#include "game/game_state.h"
#include "game/hole_loader.h"
#include "game/play_area.h"
#include "game/progress_rules.h"
#include "game/scorecard.h"
#include "game/text_ids.h"
#include "game/text_assets.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <optional>

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

// Starts hub hole `index` (the last holed ball picked up first) and holes
// out in `strokes`.
void play_hub_hole(game_state& state, const std::size_t index, const int strokes) {
    pick_up_from_cup(state);
    REQUIRE(start_hub_hole(state, index));
    finish_hole(state, strokes);
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
    CHECK(cached.hub_pin_markers == fresh.hub_pin_markers);
    CHECK(cached.collectibles == fresh.collectibles);
}
}

TEST_CASE("a course with a missing hole or world does not start") {
    game_state state = make_game_state(fixture_content(), save_data{});
    course_definition missing_hole = fixture_hub_course();
    missing_hole.holes[1] = "missing_hole";
    CHECK(!start_course(state, missing_hole));

    course_definition missing_world = fixture_hub_course();
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
    CHECK(!shot_playing(state));
}

TEST_CASE("the hub terrain covers every hole start") {
    const game_state state = started_game(fixture_hub_course());
    for (const course_world_hole_start& start : state.hub->world.hole_starts) {
        const terrain_sample ground = sample_area(state.area, start.position);
        CHECK(ground.inside_surface);
    }
}

namespace {
hole_data rising_hole(const float rise) {
    hole_data hole;
    hole.tee_position = glm::vec3(0.0f);
    hole.pin_position = glm::vec3(0.0f, rise, 200.0f);
    hole.spline.control_points = {glm::vec3(0.0f), glm::vec3(0.0f, rise * 0.5f, 100.0f), hole.pin_position};
    hole.spline.width = 20.0f;
    hole.spline.rough_width = 32.0f;
    return hole;
}

// Land over x and z from -200 to 200 and -200 to 260, every `cell` metres,
// at `height(x, z)`.
template <typename height_function>
height_grid land_grid(const float cell, const height_function& height) {
    height_grid land;
    land.origin_x = -200.0f;
    land.origin_z = -200.0f;
    land.cell_size = cell;
    land.columns = static_cast<int>(400.0f / cell) + 1;
    land.rows = static_cast<int>(460.0f / cell) + 1;
    for (int row = 0; row < land.rows; ++row) {
        for (int column = 0; column < land.columns; ++column) {
            land.heights.push_back(height(land.origin_x + cell * static_cast<float>(column), land.origin_z + cell * static_cast<float>(row)));
        }
    }
    return land;
}

// Two parallel holes `spacing` m apart, a flat one and one whose start is
// `second_height` up and which climbs `second_rise`, on `land`.
play_area two_hole_course(const height_grid& land,
                          const float spacing = 80.0f,
                          const float second_height = 10.0f,
                          const float second_rise = 15.0f) {
    course_world_definition world;
    world.hole_starts.resize(2);
    world.hole_starts[0].hole_index = 0;
    world.hole_starts[1].hole_index = 1;
    world.hole_starts[1].position = glm::vec3(spacing, second_height, 0.0f);
    world.ground = land;
    return build_course_area({rising_hole(0.0f), rising_hole(second_rise)}, world, shipped_content().tuning);
}

play_area two_hole_course(const float land_height,
                          const float spacing = 80.0f,
                          const float second_height = 10.0f,
                          const float second_rise = 15.0f) {
    return two_hole_course(land_grid(50.0f, [land_height](float, float) { return land_height; }), spacing, second_height, second_rise);
}
}

TEST_CASE("on land a hole lies on the land, lifted by its own heights") {
    // The land slopes; the second hole's start height is not its height.
    const play_area course = two_hole_course(land_grid(50.0f, [](const float x, const float z) { return 30.0f + 0.05f * x + 0.02f * z; }));
    for (float z = 20.0f; z <= 180.0f; z += 7.3f) {
        for (float x = -8.0f; x <= 8.0f; x += 2.9f) {
            const float land = 30.0f + 0.05f * x + 0.02f * z;
            CHECK(near(sample_area(course, glm::vec3(x, 0.0f, z)).point.y, land, 0.1f));
            const float second = land + 0.05f * 80.0f + 15.0f * z / 200.0f;
            CHECK(near(sample_area(course, glm::vec3(80.0f + x, 0.0f, z)).point.y, second, 0.3f));
        }
    }
}

TEST_CASE("off the holes the ball lands on the ground, which follows the land far away") {
    const play_area course = two_hole_course(30.0f);
    const terrain_sample far = sample_area(course, glm::vec3(250.0f, 0.0f, 100.0f));
    CHECK(near(far.point.y, 30.0f, 0.01f));
    CHECK(far.material == terrain_material::rough);

}

TEST_CASE("a hole's rough eases its lift into the land") {
    // The climbing hole is 7.5 m up halfway; its fairway is 20 m wide and its ribbon 32 m.
    const play_area course = two_hole_course(3.0f);
    CHECK(near(sample_area(course, glm::vec3(72.5f, 0.0f, 100.0f)).point.y, 10.5f, 0.3f));
    CHECK(near(sample_area(course, glm::vec3(63.5f, 0.0f, 100.0f)).point.y, 3.0f, 0.3f));
    const float halfway = sample_area(course, glm::vec3(67.0f, 0.0f, 100.0f)).point.y;
    CHECK(halfway > 4.0f);
    CHECK(halfway < 9.5f);
}

TEST_CASE("overlapping holes at different heights make one continuous surface") {
    // 20 m apart with 32 m wide ribbons: they overlap by 12 m, 3 m apart in height halfway.
    const play_area course = two_hole_course(0.0f, 20.0f, 0.0f, 6.0f);
    float previous = sample_area(course, glm::vec3(-20.0f, 0.0f, 100.0f)).point.y;
    for (float x = -19.5f; x <= 40.0f; x += 0.5f) {
        const float y = sample_area(course, glm::vec3(x, 0.0f, 100.0f)).point.y;
        CHECK(std::abs(y - previous) < 0.5f);
        previous = y;
    }
    // Deep inside each hole, that hole's own height.
    CHECK(near(sample_area(course, glm::vec3(-2.0f, 0.0f, 100.0f)).point.y, 0.0f, 0.3f));
    CHECK(near(sample_area(course, glm::vec3(22.0f, 0.0f, 100.0f)).point.y, 3.0f, 0.3f));
}

TEST_CASE("on a fairway the ground keeps land detail its coarse cells would lose, without cracks") {
    // Bumps 12.6 m apart on a 2 m grid: a 4 m cell cuts across them, a 2 m one follows them.
    const auto bumps = [](const float x, const float z) { return std::sin(x * 0.5f) * std::sin(z * 0.5f); };
    const play_area course = two_hole_course(land_grid(2.0f, bumps), 80.0f, 0.0f, 0.0f);
    float fairway_error = 0.0f;
    float open_error = 0.0f;
    for (float z = 20.0f; z <= 180.0f; z += 0.37f) {
        for (float x = -8.0f; x <= 8.0f; x += 0.53f) {
            fairway_error = std::max(fairway_error, std::abs(sample_area(course, glm::vec3(x, 0.0f, z)).point.y - bumps(x, z)));
            const float open_x = x + 150.0f;
            open_error = std::max(open_error, std::abs(sample_area(course, glm::vec3(open_x, 0.0f, z)).point.y - bumps(open_x, z)));
        }
    }
    CHECK(fairway_error < 0.3f);
    CHECK(open_error > 0.5f);
    // Across the fairway's edge, where its finer cells meet the coarse ones, the
    // surface has no step.
    for (float z = 30.0f; z <= 170.0f; z += 3.1f) {
        float previous = sample_area(course, glm::vec3(-30.0f, 0.0f, z)).point.y;
        for (float x = -29.95f; x <= 30.0f; x += 0.05f) {
            const float y = sample_area(course, glm::vec3(x, 0.0f, z)).point.y;
            CHECK(std::abs(y - previous) < 0.1f);
            previous = y;
        }
    }
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

TEST_CASE("a hub hole is played on the whole course") {
    game_state state = started_game(fixture_hub_course());
    const glm::vec3 course_center = state.area.center;
    REQUIRE(start_hub_hole(state, 0));
    CHECK(near(state.area.center, course_center));
    CHECK(sample_area(state.area, state.hub->world.hole_starts[2].position).inside_surface);
    CHECK(state.hole->wind_seed == state.course_holes[0].wind_seed);
}

TEST_CASE("action at a hole start plays that hole where the hub shows it") {
    game_state state = started_game(fixture_hub_course());
    const hub_hole_marker marker = state.hub->markers[2];
    walk_to(state, state.hub->world.hole_starts[2].position);

    update_game(state, action_input(), 0.016f);

    REQUIRE(state.hole.has_value());
    CHECK(state.hole->index == 2);
    CHECK(state.round.current_hole_index == 2);
    CHECK(near(horizontal(state.hole->tee_position), horizontal(marker.tee_position), 0.001f));
    CHECK(near(horizontal(state.hole->pin_position), horizontal(marker.pin_position), 0.001f));
    CHECK(horizontal_distance(state.ball.position, marker.tee_position) < 0.5f);
    CHECK(sample_area(state.area, marker.pin_position).inside_surface);
}

TEST_CASE("the action that starts a hole takes up its tee ball, so the tee shot needs no extra press") {
    game_state state = started_game(fixture_hub_course());
    walk_to(state, state.hub->world.hole_starts[0].position);

    update_game(state, action_input(), 0.016f);
    REQUIRE(state.hole.has_value());
    CHECK(state.mode == game_mode::aiming);

    // Address, start the meter, hit: as from anywhere beside the ball.
    for (int press = 0; press < 3; ++press) {
        update_game(state, action_input(), 0.016f);
    }
    CHECK(state.stroke_count == 1);
    CHECK(shot_playing(state));
}

TEST_CASE("holing out on a hub hole leaves the player where they stand") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(start_hub_hole(state, 1));
    const glm::vec3 stance = state.player.position;
    const glm::vec3 cup = pin_anchor_position(state);
    REQUIRE(horizontal_distance(stance, cup) > 10.0f);

    state.stroke_count = 1;
    play_shot(state, shot_result{cup, true, 0.05f, {state.ball.position, cup}, {}});
    for (int i = 0; i < 20 && state.hole; ++i) {
        update_game(state, game_input{}, 0.02f);
    }

    CHECK(in_hub(state));
    CHECK(*state.round.strokes[1] == 1);
    CHECK(state.round.current_hole_index == 2);
    CHECK(near(horizontal(state.player.position), horizontal(stance), 0.001f));
    CHECK(state.save_requested);
}

TEST_CASE("hub holes can be played in any order and the round ends after all of them") {
    game_state state = started_game(fixture_hub_course());

    REQUIRE(start_hub_hole(state, 2));
    finish_hole(state, 5);
    CHECK(!round_finished(state.round));
    CHECK(state.save.completed_course_ids.empty());
    pick_up_from_cup(state);
    CHECK(!start_hub_hole(state, 2));

    REQUIRE(start_hub_hole(state, 0));
    finish_hole(state, 3);
    pick_up_from_cup(state);
    REQUIRE(start_hub_hole(state, 1));
    finish_hole(state, 4);

    CHECK(round_finished(state.round));
    CHECK(state.save.completed_course_ids == std::vector<std::string>{"fixture_hub"});
}

TEST_CASE("the holed ball waits in its cup until it is picked up, and no hole starts before") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(start_hub_hole(state, 0));
    const glm::vec3 cup = pin_anchor_position(state);
    finish_hole(state, 3);
    REQUIRE(in_hub(state));
    REQUIRE(state.cup_ball.has_value());
    CHECK(near(*state.cup_ball, cup));

    CHECK(!start_hub_hole(state, 1));
    REQUIRE(state.notice.has_value());
    CHECK(state.notice->text_key == text_ball_in_cup);

    const float reach = state.tuning.player.ball_interact_radius;
    walk_to(state, cup + glm::vec3(reach + 1.0f, 0.0f, 0.0f));
    CHECK(!cup_ball_in_reach(state));
    CHECK(!pick_up_cup_ball(state));
    CHECK(state.cup_ball.has_value());

    walk_to(state, cup + glm::vec3(reach * 0.5f, 0.0f, 0.0f));
    CHECK(cup_ball_in_reach(state));
    update_game(state, action_input(), 0.016f);
    CHECK(in_hub(state));
    CHECK(!state.cup_ball.has_value());
    CHECK(start_hub_hole(state, 1));
}

TEST_CASE("the ball in the cup stays into the next round and goes when the course is left") {
    game_state state = started_game(fixture_hub_course());
    for (std::size_t i = 0; i < state.course_holes.size(); ++i) {
        REQUIRE(start_hub_hole(state, i));
        finish_hole(state, 3);
        pick_up_from_cup(state);
    }
    REQUIRE(round_finished(state.round));
    REQUIRE(state.cup_ball.has_value());  // not picked up: the round ended

    start_next_round(state);
    CHECK(state.cup_ball.has_value());
    CHECK(!start_hub_hole(state, 0));

    REQUIRE(start_course(state, state.course));
    CHECK(!state.cup_ball.has_value());
    CHECK(start_hub_hole(state, 0));
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
    game_state state = started_hole();
    check_anchors_are_fresh(state);

    std::uint64_t revision = state.terrain_render_revision;
    REQUIRE(start_course(state, fixture_hub_course()));
    CHECK(state.terrain_render_revision > revision);
    check_anchors_are_fresh(state);
    CHECK(state.static_anchors.trees.size() == state.area.trees.size());
    CHECK(state.static_anchors.hub_pin_markers.size() == 3U);
    CHECK(state.static_anchors.collectibles.size() == 3U);
    const std::vector<tree_body> hub_trees = state.static_anchors.trees;

    revision = state.terrain_render_revision;
    walk_to(state, state.hub->world.hole_starts[1].position);
    update_game(state, action_input(), 0.016f);
    REQUIRE(state.hole.has_value());
    CHECK(state.terrain_render_revision > revision);
    check_anchors_are_fresh(state);
    CHECK(state.static_anchors.hub_pin_markers.size() == 3U);

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
    game_state state = started_game(fixture_hub_course());
    const string_table strings = shipped_strings();

    play_hub_hole(state, 0, 4);
    scorecard_data card = build_scorecard_data(state, strings);
    REQUIRE(card.rows.size() == 3U);
    CHECK(card.course_name == "Fixture Hub");
    CHECK(card.current_hole_index == 1U);
    CHECK(card.rows[0].hole_name == "New Hole");
    CHECK(card.rows[0].played);
    CHECK(card.rows[0].strokes == 4);
    CHECK(card.rows[0].relative_label == "+1");
    CHECK(card.rows[1].hole_name == "The Ditch");
    CHECK(!card.rows[1].played);
    CHECK(card.total_par == 3);
    CHECK(card.total_strokes == 4);

    play_hub_hole(state, 1, 2);
    play_hub_hole(state, 2, 3);
    card = build_scorecard_data(state, strings);
    CHECK(card.finished);
    CHECK(card.total_par == 9);
    CHECK(card.total_strokes == 9);
    CHECK(card.total_relative_label == "EVEN");
}

TEST_CASE("a new course starts with a clean scorecard") {
    game_state state = started_hole();
    finish_hole(state, 4);

    REQUIRE(start_course(state, fixture_hub_course()));
    const scorecard_data card = build_scorecard_data(state, shipped_strings());

    REQUIRE(card.rows.size() == 3U);
    CHECK(!card.rows[0].played);
    CHECK(card.total_strokes == 0);
}

TEST_CASE("relative scores read over, under and even") {
    const string_table strings = shipped_strings();
    CHECK(format_relative_score(strings, 2) == "+2");
    CHECK(format_relative_score(strings, -1) == "-1");
    CHECK(format_relative_score(strings, 0) == "EVEN");
}
