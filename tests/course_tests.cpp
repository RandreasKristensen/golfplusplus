#include "doctest.h"

#include "game/course_session.h"
#include "game/game_state.h"
#include "game/hole_loader.h"
#include "game/interactables.h"
#include "game/mode_dispatch.h"
#include "game/play_area.h"
#include "game/progress_rules.h"
#include "game/scorecard.h"
#include "game/text_ids.h"
#include "game/text_assets.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

namespace {
string_table shipped_strings() {
    return load_text_assets(asset_root())->strings;
}

// Completes the hole my ball is in play on with `strokes`.
void finish_hole(game_state& state, const int strokes) {
    REQUIRE(ball_in_play(state));
    state.ball->strokes = strokes;
    complete_current_hole(state);
}

// Tees up on hole `index` (the last holed ball picked up first) and holes
// out in `strokes`.
void play_course_hole(game_state& state, const std::size_t index, const int strokes) {
    pick_up_from_cup(state);
    REQUIRE(tee_up(state, index, false));
    finish_hole(state, strokes);
}

// A shot that holes out at once, then plays out.
void hole_out(game_state& state) {
    const glm::vec3 cup = pin_anchor_position(state, state.ball->hole);
    state.ball->strokes = 1;
    play_shot(state, shot_result{cup, true, 0.05f, {state.ball->position, cup}, {}});
    for (int i = 0; i < 20 && ball_in_play(state); ++i) {
        update_game(state, game_input{}, 0.02f);
    }
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
    CHECK(cached.tees == fresh.tees);
    CHECK(cached.pins == fresh.pins);
    CHECK(same_trees(cached.trees, fresh.trees));
    CHECK(cached.collectibles == fresh.collectibles);
}
}

TEST_CASE("a course with a missing hole or world does not start") {
    game_state state = make_game_state(fixture_content(), save_data{});
    course_definition missing_hole = fixture_hub_course();
    missing_hole.holes[1] = "missing_hole";
    CHECK(!start_course(state, missing_hole));

    course_definition missing_world = fixture_hub_course();
    missing_world.world = "world/courses/missing/world.json";
    CHECK(!start_course(state, missing_world));
    CHECK(state.course_holes.empty());
}

TEST_CASE("a course starts at hole 1's start facing down hole 1, my ball in my pocket") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(course_started(state));
    CHECK(!state.ball.has_value());
    CHECK(near(horizontal(state.player.position), horizontal(state.world.hole_starts[0].position), 0.001f));
    const std::optional<interactable> near_me = nearest_interactable(state);
    REQUIRE(near_me.has_value());
    CHECK(near_me->kind == interactable_kind::tee);
    CHECK(near_me->index == 0U);
    CHECK(near(state.player.yaw, yaw_towards(state.player.position, state.holes[0].pin_position), 0.0001f));
    CHECK(!shot_playing(state));
}

TEST_CASE("the course's terrain covers every hole start") {
    const game_state state = started_game(fixture_hub_course());
    for (const course_world_hole_start& start : state.world.hole_starts) {
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

// The shipped tuning with the land's relief as it is.
game_tuning unscaled_relief() {
    game_tuning tuning = shipped_content().tuning;
    tuning.terrain.relief.scale = 1.0f;
    tuning.terrain.relief.green_scale = 1.0f;
    return tuning;
}

course_world_definition two_hole_world(const height_grid& land, const float spacing, const float second_height) {
    course_world_definition world;
    world.hole_starts.resize(2);
    world.hole_starts[0].hole_index = 0;
    world.hole_starts[1].hole_index = 1;
    world.hole_starts[1].position = glm::vec3(spacing, second_height, 0.0f);
    world.ground = land;
    return world;
}

// Two parallel holes `spacing` m apart, a flat one and one whose start is
// `second_height` up and which climbs `second_rise`, on `land` as it is.
play_area two_hole_course(const height_grid& land,
                          const float spacing = 80.0f,
                          const float second_height = 10.0f,
                          const float second_rise = 15.0f) {
    return build_course_area({rising_hole(0.0f), rising_hole(second_rise)}, two_hole_world(land, spacing, second_height),
                             unscaled_relief());
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

TEST_CASE("the land's relief is scaled about its mean, greens flatter, and what stands on it stays on it") {
    const auto slope = [](const float x, const float z) { return 30.0f + 0.05f * x + 0.02f * z; };
    const height_grid land = land_grid(2.0f, slope);
    hole_data with_green = rising_hole(0.0f);
    const material_zone green = circle_zone(material_zone_type::green, glm::vec3(0.0f, 0.0f, 190.0f), 12.0f);
    const material_zone pond = circle_zone(material_zone_type::water, glm::vec3(-60.0f, 0.0f, 60.0f), 10.0f);
    with_green.material_zones = {green, pond};
    const game_tuning tuning = shipped_content().tuning;
    const land_relief relief = tuning.terrain.relief;
    REQUIRE(relief.scale > 1.0f);
    REQUIRE(relief.green_scale < 1.0f);
    const play_area course = build_course_area({with_green, rising_hole(0.0f)}, two_hole_world(land, 80.0f, 0.0f), tuning);

    // Off the greens, the slope is steeper by the scale about the land's mean
    // height (the middle of the grid, for a plane).
    const float mean = slope(0.0f, 30.0f);
    for (const glm::vec3 point : {glm::vec3(150.0f, 0.0f, -150.0f), glm::vec3(-150.0f, 0.0f, 200.0f), glm::vec3(40.0f, 0.0f, 100.0f)}) {
        CHECK(near(sample_area(course, point).point.y, mean + relief.scale * (slope(point.x, point.z) - mean), 0.05f));
    }
    // On the green, its slope is the land's times green_scale, about the
    // green's scaled mean height; past its edge the land's own slope returns.
    const float green_mean = mean + relief.scale * (slope(green.center.x, green.center.z) - mean);
    const auto green_height = [&](const float x) { return sample_area(course, glm::vec3(x, 0.0f, green.center.z)).point.y; };
    CHECK(near(green_height(0.0f), green_mean, 0.05f));
    CHECK(near((green_height(6.0f) - green_height(-6.0f)) / 12.0f, 0.05f * relief.green_scale, 0.002f));
    const float beyond = zone_half_extent(green).x + relief.green_blend_distance + 4.0f;
    CHECK(near((green_height(beyond + 2.0f) - green_height(beyond)) / 2.0f, 0.05f * relief.scale, 0.002f));
    float previous = green_height(-40.0f);
    for (float x = -39.8f; x <= 40.0f; x += 0.2f) {
        CHECK(std::abs(green_height(x) - previous) < 0.05f);
        previous = green_height(x);
    }

    // The pin is anchored on the scaled green, the tee box sits on the scaled
    // ground and the pond fills its scaled bowl to its lowest rim.
    REQUIRE(course.tee_boxes.size() == 2);
    const tee_box& box = course.tee_boxes.front();
    CHECK(box.center.y > box.bottom);
    CHECK(box.center.y - sample_terrain_mesh(course.ground, box.center, course.ground_y).point.y < 1.0f);
    const glm::vec3 pin = with_green.pin_position;
    CHECK(near(anchor_on_terrain(course, pin).y,
               green_mean + relief.green_scale * (slope(pin.x, pin.z) - slope(green.center.x, green.center.z)), 0.02f));
    const auto scaled = [&](const glm::vec2& at) { return mean + relief.scale * (slope(at.x, at.y) - mean); };
    float rim = std::numeric_limits<float>::max();
    for (const glm::vec2& point : pond.outline) {
        rim = std::min(rim, scaled(point));
    }
    REQUIRE(course.water_levels.size() == 2);
    CHECK(near(course.water_levels[1], rim, 0.02f));
    const glm::vec3 past = pond.center - glm::normalize(glm::vec3(0.05f, 0.0f, 0.02f)) * (zone_half_extent(pond).x + 3.0f);
    CHECK(near(sample_area(course, past).point.y, mean + relief.scale * (slope(past.x, past.z) - mean), 0.02f));
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

TEST_CASE("a hole is played on the whole course") {
    game_state state = started_game(fixture_hub_course());
    const glm::vec3 course_center = state.area.center;
    REQUIRE(tee_up(state, 0, false));
    CHECK(near(state.area.center, course_center));
    CHECK(sample_area(state.area, state.world.hole_starts[2].position).inside_surface);
}

TEST_CASE("interacting at a hole's tee tees my ball up on that hole where the course has it") {
    game_state state = started_game(fixture_hub_course());
    const course_hole hole = state.holes[2];
    walk_to(state, state.world.hole_starts[2].position);

    update_game(state, interact_input(), 0.016f);

    REQUIRE(state.ball.has_value());
    CHECK(state.ball->hole == 2U);
    CHECK(state.ball->place == ball_place::teed);
    CHECK(state.ball->strokes == 0);
    CHECK(state.round.current_hole_index == 2);
    CHECK(horizontal_distance(state.ball->position, hole.tee_position) < 0.5f);
    CHECK(near(horizontal(pin_anchor_position(state, 2)), horizontal(hole.pin_position), 0.001f));
    CHECK(sample_area(state.area, hole.pin_position).inside_surface);
}

TEST_CASE("interacting at a hole's tee tees up and steps up to the ball, so Space hits it") {
    game_state state = started_game(fixture_hub_course());
    walk_to(state, state.world.hole_starts[0].position);

    update_game(state, interact_input(), 0.016f);
    REQUIRE(ball_in_play(state));
    CHECK(state.mode == game_mode::addressing);

    // Swing: as from anywhere beside the ball.
    update_game(state, swing_input(), 0.016f);
    swing_through(state);
    CHECK(state.ball->strokes == 1);
    CHECK(state.ball->place == ball_place::at_rest);
    CHECK(shot_playing(state));
}

TEST_CASE("holing out leaves the player where they stand and the ball in the cup") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(tee_up(state, 1, false));
    const glm::vec3 stance = state.player.position;
    const glm::vec3 cup = pin_anchor_position(state, 1);
    REQUIRE(horizontal_distance(stance, cup) > 10.0f);

    hole_out(state);

    REQUIRE(state.ball.has_value());
    CHECK(state.ball->place == ball_place::in_cup);
    CHECK(state.ball->hole == 1U);
    CHECK(near(state.ball->position, cup));
    CHECK(*state.round.strokes[1] == 1);
    CHECK(state.round.current_hole_index == 2);
    CHECK(near(horizontal(state.player.position), horizontal(stance), 0.001f));
    CHECK(state.save_requested);
}

TEST_CASE("holing out keeps the view on the cup, not the next tee") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(tee_up(state, 1, false));
    const glm::vec3 cup = pin_anchor_position(state, 1);
    state.player.yaw = wrap_angle(state.player.yaw + 1.0f);
    state.look.pitch = -0.2f;

    hole_out(state);

    REQUIRE(!ball_in_play(state));
    CHECK(near(state.player.yaw, yaw_towards(state.player.position, cup), 0.0001f));
    CHECK(near(state.look.pitch, -0.2f, 0.0001f));
}

TEST_CASE("a hole completing ends nothing else: my seat in my cart, a lit cigarette, an item's effect") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(tee_up(state, 1, false));
    update_game(state, [] {
        game_input smoke;
        smoke.use_smokable = true;
        return smoke;
    }(), 0.016f);
    REQUIRE(state.smoking.has_value());
    item_effect quicker;
    quicker.target = effect_target_walk_speed;
    quicker.bucket = effect_bucket::increased;
    quicker.value = 0.5f;
    quicker.duration = effect_duration::seconds;
    quicker.seconds = 60.0f;
    state.effects.push_back(active_effect{"shoes", quicker, shot_unix_ms(state), shot_unix_ms(state) + 60000});
    const glm::vec3 cup = pin_anchor_position(state, 1);
    state.ball->strokes = 1;
    play_shot(state, shot_result{cup, true, 5.0f, {state.ball->position, cup}, {}});
    // Online the hole may complete only after I have stepped into my cart.
    state.mode = game_mode::walking;
    state.shot.reset();
    sit_in_cart(state);
    const glm::vec3 seated = state.player.position;

    complete_current_hole(state);

    CHECK(state.ride.has_value());
    CHECK(driving_cart(state));
    CHECK(near(state.player.position, seated));
    CHECK(state.smoking.has_value());
    CHECK(state.effects.size() == 1U);
    REQUIRE(state.ball.has_value());
    CHECK(state.ball->place == ball_place::in_cup);
}

TEST_CASE("holes can be played in any order and the round ends after all of them") {
    game_state state = started_game(fixture_hub_course());

    REQUIRE(tee_up(state, 2, false));
    finish_hole(state, 5);
    CHECK(!round_finished(state.round));
    CHECK(state.save.completed_course_ids.empty());
    pick_up_from_cup(state);
    CHECK(!tee_up(state, 2, false));

    REQUIRE(tee_up(state, 0, false));
    finish_hole(state, 3);
    pick_up_from_cup(state);
    REQUIRE(tee_up(state, 1, false));
    finish_hole(state, 4);

    CHECK(round_finished(state.round));
    CHECK(state.save.completed_course_ids == std::vector<std::string>{"fixture_hub"});
}

TEST_CASE("the holed ball waits in its cup until it is picked up, and no hole tees up before") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(tee_up(state, 0, false));
    const glm::vec3 cup = pin_anchor_position(state, 0);
    finish_hole(state, 3);
    REQUIRE(state.ball.has_value());
    CHECK(state.ball->place == ball_place::in_cup);
    CHECK(near(state.ball->position, cup));

    CHECK(!tee_up(state, 1, true));
    REQUIRE(state.notice.has_value());
    CHECK(state.notice->text_key == text_ball_in_cup);

    const float reach = state.tuning.player.ball_interact_radius;
    walk_to(state, cup + glm::vec3(reach + 1.0f, 0.0f, 0.0f));
    CHECK(!cup_ball_in_reach(state));
    CHECK(!pick_up_cup_ball(state));
    CHECK(state.ball.has_value());

    walk_to(state, cup + glm::vec3(reach * 0.5f, 0.0f, 0.0f));
    CHECK(cup_ball_in_reach(state));
    update_game(state, interact_input(), 0.016f);
    CHECK(!state.ball.has_value());
    CHECK(tee_up(state, 1, false));
}

TEST_CASE("the ball in the cup stays into the next round and goes when the course is left") {
    game_state state = started_game(fixture_hub_course());
    for (std::size_t i = 0; i < state.course_holes.size(); ++i) {
        pick_up_from_cup(state);
        REQUIRE(tee_up(state, i, false));
        finish_hole(state, 3);
    }
    REQUIRE(round_finished(state.round));
    REQUIRE(state.ball.has_value());  // not picked up: the round ended
    CHECK(state.ball->place == ball_place::in_cup);

    start_next_round(state);
    CHECK(state.ball.has_value());
    CHECK(!tee_up(state, 0, false));

    REQUIRE(start_course(state, state.course));
    CHECK(!state.ball.has_value());
    CHECK(tee_up(state, 0, false));
}

TEST_CASE("tees of played holes cannot be used again, nor my ball's own") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(tee_up(state, 0, false));
    walk_to(state, state.world.hole_starts[0].position + glm::vec3(0.0f, 0.0f, -3.0f));
    for (const interactable& thing : interactables_in_reach(state)) {
        CHECK(thing.kind != interactable_kind::tee);
    }
    CHECK(!tee_up(state, 0, true));
    CHECK(state.ball->strokes == 0);

    finish_hole(state, 3);
    pick_up_from_cup(state);
    walk_to(state, state.world.hole_starts[0].position);
    for (const interactable& thing : interactables_in_reach(state)) {
        CHECK(thing.kind != interactable_kind::tee);
    }
}

namespace {
// My ball teed on hole 1 and me at hole 3's tee.
game_state ball_in_play_at_another_tee() {
    game_state state = started_game(fixture_hub_course());
    CHECK(tee_up(state, 0, false));
    state.ball->strokes = 2;
    state.ball->place = ball_place::at_rest;
    walk_to(state, state.world.hole_starts[2].position);
    return state;
}

game_input cancel_input() {
    game_input input;
    input.cancel = true;
    return input;
}
}

TEST_CASE("F at another tee with my ball in play asks first, and yes gives the hole up with no score") {
    game_state state = ball_in_play_at_another_tee();

    update_game(state, interact_input(), 0.016f);
    REQUIRE(state.give_up_prompt == std::optional<std::size_t>(2));
    CHECK(state.ball->hole == 0U);
    CHECK(!interact_available(state));

    update_game(state, interact_input(), 0.016f);
    CHECK(!state.give_up_prompt.has_value());
    REQUIRE(state.ball.has_value());
    CHECK(state.ball->hole == 2U);
    CHECK(state.ball->strokes == 0);
    CHECK(state.mode == game_mode::addressing);
    CHECK(!hole_played(state.round, 0));
    CHECK(state.save.holes_completed == 0);
}

TEST_CASE("Esc, or walking away, keeps playing the hole the tee asked about") {
    game_state state = ball_in_play_at_another_tee();
    update_game(state, interact_input(), 0.016f);
    REQUIRE(state.give_up_prompt.has_value());
    update_game(state, cancel_input(), 0.016f);
    CHECK(!state.give_up_prompt.has_value());
    CHECK(state.ball->hole == 0U);
    CHECK(state.ball->strokes == 2);

    update_game(state, interact_input(), 0.016f);
    REQUIRE(state.give_up_prompt.has_value());
    walk_to(state, state.world.hole_starts[2].position + glm::vec3(200.0f, 0.0f, 0.0f));
    update_game(state, game_input{}, 0.016f);
    CHECK(!state.give_up_prompt.has_value());
    CHECK(state.ball->hole == 0U);
}

TEST_CASE("teeing up refuses a ball in play elsewhere unless giving it up") {
    game_state state = ball_in_play_at_another_tee();
    CHECK(!tee_up(state, 2, false));
    REQUIRE(state.notice.has_value());
    CHECK(state.notice->text_key == text_ball_in_play);
    CHECK(state.ball->hole == 0U);
    CHECK(tee_up(state, 2, true));
    CHECK(state.ball->hole == 2U);
}

TEST_CASE("giving up a hole puts my ball in my pocket and leaves me where I stand") {
    game_state state = ball_in_play_at_another_tee();
    const glm::vec3 standing = state.player.position;
    give_up_hole(state);
    CHECK(!state.ball.has_value());
    CHECK(near(state.player.position, standing));
    CHECK(!hole_played(state.round, 0));
    CHECK(tee_up(state, 0, false));
}

TEST_CASE("claiming a collectible gives its xp as an xp drop, with my ball in play too") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(tee_up(state, 0, false));
    const course_world_collectible& lost_ball = state.world.collectibles[1];
    walk_to(state, lost_ball.position);

    update_game(state, interact_input(), 0.016f);

    CHECK(state.save.collected_ids == std::vector<std::string>{"lost_ball"});
    CHECK(state.save.world_flags == std::vector<std::string>{"found_lost_ball"});
    CHECK(skill_xp(state.save.skills, "fitness") >= 12);
    CHECK(!state.xp_drops.empty());
    for (const interactable& thing : interactables_in_reach(state)) {
        CHECK(thing.kind != interactable_kind::collectible);
    }
    CHECK(ball_in_play(state));
}

TEST_CASE("the nearest thing in reach is what F acts on") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(tee_up(state, 0, false));
    const glm::vec3 ball = state.ball->position;
    const glm::vec3 seat = ball + glm::vec3(2.0f, 0.0f, 0.0f);
    state.cart.pose = cart_around_seat(seat, 0.0f, cart_seat::driver, state.tuning.cart);
    REQUIRE(horizontal_distance(seat_position(state.cart.pose, cart_seat::driver, state.tuning.cart), seat) < 0.001f);

    // Half a metre from my ball, the seat further: the ball.
    state.player.position = ball + glm::vec3(0.5f, 0.0f, 0.0f);
    REQUIRE(interactables_in_reach(state).size() >= 2U);
    std::optional<interactable> near_me = nearest_interactable(state);
    REQUIRE(near_me.has_value());
    CHECK(near_me->kind == interactable_kind::ball);

    // Next to the seat, my ball further: the seat.
    state.player.position = ball + glm::vec3(1.8f, 0.0f, 0.0f);
    near_me = nearest_interactable(state);
    REQUIRE(near_me.has_value());
    CHECK(near_me->kind == interactable_kind::seat);
}

TEST_CASE("the cart pulls away quicker on a road and earns road xp only there, with my ball in play too") {
    game_state on_road = started_game(fixture_hub_course());
    REQUIRE(tee_up(on_road, 0, false));
    on_road.mode = game_mode::walking;
    const std::vector<glm::vec3>& road = on_road.world.cart_roads.front().polyline;
    walk_to(on_road, road.front());
    on_road.player.yaw = yaw_towards(road[0], road[1]);

    game_state off_road = on_road;
    walk_to(off_road, glm::vec3(200.0f, 0.0f, 400.0f));
    CHECK(cart_on_road(on_road));
    CHECK(!cart_on_road(off_road));

    sit_in_cart(on_road);
    sit_in_cart(off_road);
    game_input drive;
    drive.forward_held = true;
    update_game(on_road, drive, 0.05f);
    update_game(off_road, drive, 0.05f);
    CHECK(my_cart_speed(on_road) > my_cart_speed(off_road));

    for (game_state* state : {&on_road, &off_road}) {
        for (int i = 0; i < 160; ++i) {
            update_game(*state, drive, 0.05f);
        }
    }
    CHECK(skill_xp(on_road.save.skills, on_road.rewards.cart_on_road.skill_id) > 0);
    CHECK(skill_xp(off_road.save.skills, off_road.rewards.cart_on_road.skill_id) == 0);
}

TEST_CASE("road reach uses each road's own width") {
    game_state state = started_game(fixture_hub_course());
    course_world_cart_road wide;
    wide.width = 40.0f;
    wide.polyline = {glm::vec3(1000.0f, 0.0f, 0.0f), glm::vec3(1100.0f, 0.0f, 0.0f)};
    state.world.cart_roads.push_back(wide);

    // 10 m from the narrow road: outside its reach even though another road is wide.
    walk_to(state, glm::vec3(100.0f, 0.0f, -40.0f));
    CHECK(!cart_on_road(state));
    walk_to(state, glm::vec3(1050.0f, 0.0f, 15.0f));
    CHECK(cart_on_road(state));
}

TEST_CASE("static anchors stay fresh, and my ball never moves the terrain revision") {
    game_state state = started_hole();
    check_anchors_are_fresh(state);

    std::uint64_t revision = state.terrain_render_revision;
    REQUIRE(start_course(state, fixture_hub_course()));
    CHECK(state.terrain_render_revision > revision);
    check_anchors_are_fresh(state);
    CHECK(state.static_anchors.trees.size() == state.area.trees.size());
    CHECK(state.static_anchors.pins.size() == 3U);
    CHECK(state.static_anchors.tees.size() == 3U);
    CHECK(state.static_anchors.collectibles.size() == 3U);
    const std::vector<tree_body> course_trees = state.static_anchors.trees;

    revision = state.terrain_render_revision;
    walk_to(state, state.world.hole_starts[1].position);
    update_game(state, interact_input(), 0.016f);
    REQUIRE(ball_in_play(state));
    // The course is the same place: nothing is rebuilt or re-uploaded.
    CHECK(state.terrain_render_revision == revision);
    check_anchors_are_fresh(state);

    finish_hole(state, 3);
    CHECK(state.terrain_render_revision == revision);
    pick_up_from_cup(state);
    CHECK(state.terrain_render_revision == revision);
    REQUIRE(tee_up(state, 2, false));
    give_up_hole(state);
    CHECK(state.terrain_render_revision == revision);
    check_anchors_are_fresh(state);
    CHECK(same_trees(state.static_anchors.trees, course_trees));
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

    play_course_hole(state, 0, 4);
    scorecard_data card = build_scorecard_data(state, strings);
    REQUIRE(card.pars.size() == 3U);
    REQUIRE(card.players.size() == 1U);
    CHECK(card.course_name == "Fixture Hub");
    CHECK(card.current_hole_index == 1U);
    CHECK(card.pars[0] == 3);
    CHECK(card.players[0].name == "SCORE");
    CHECK(!card.players[0].me);
    CHECK((card.players[0].strokes == std::vector<int>{4, 0, 0}));
    CHECK(card.players[0].total_strokes == 4);
    CHECK(card.players[0].relative_label == "+1");

    play_course_hole(state, 1, 2);
    play_course_hole(state, 2, 3);
    card = build_scorecard_data(state, strings);
    CHECK(card.finished);
    CHECK(card.players[0].total_strokes == 9);
    CHECK(card.players[0].relative_label == "EVEN");
}

TEST_CASE("a new course starts with a clean scorecard") {
    game_state state = started_hole();
    finish_hole(state, 4);

    REQUIRE(start_course(state, fixture_hub_course()));
    const scorecard_data card = build_scorecard_data(state, shipped_strings());

    REQUIRE(card.players.size() == 1U);
    CHECK(card.players[0].strokes == std::vector<int>(3, 0));
    CHECK(card.players[0].total_strokes == 0);
}

TEST_CASE("relative scores read over, under and even") {
    const string_table strings = shipped_strings();
    CHECK(format_relative_score(strings, 2) == "+2");
    CHECK(format_relative_score(strings, -1) == "-1");
    CHECK(format_relative_score(strings, 0) == "EVEN");
}
