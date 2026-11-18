#include "doctest.h"

#include "core/key_bindings.h"
#include "game/ball_lift.h"
#include "game/course_session.h"
#include "game/golf_cart.h"
#include "game/golf_bag.h"
#include "game/my_balls.h"
#include "game/interactables.h"

#include "test_support.h"

// F taps address or lifts a course cup ball; hub pickup requires a hold.
namespace {
game_state hub_with_ball() {
    game_state state = started_game(fixture_hub_course());
    state.kind = place_kind::hub; state.place.carts = false; state.place.rules = place_rules{false, true, false, true, 10};
    state.hub = hub_definition{};
    state.round.reset(); state.holes.clear(); state.course_holes.clear();
    park_cart(state); strap_bag_on_cart(state);
    game_input bag; bag.move_bag = true;
    update_game(state, bag, 0.016f);
    finish_handling(state);
    game_input drop; drop.drop_practice_ball = true;
    update_game(state, drop, 0.016f);
    return state;
}

game_state course_with_ball() {
    game_state state = started_hole();
    game_input bag; bag.move_bag = true;
    update_game(state, bag, 0.016f);
    finish_handling(state);
    return state;
}

float hold_seconds(const game_state& state) { return state.tuning.player.ball_pickup_hold_seconds + 0.1f; }

void put_ball_in_cup(game_state& state) {
    state.ball->place = ball_place::in_cup;
    state.player.position = state.ball->position;
}

// The same press sequence on a fresh hub state and a fresh course state.
template <typename Check> void for_both_places(Check&& check) {
    game_state hub = hub_with_ball();
    REQUIRE(hub.ball.has_value());
    check(hub);
    game_state course = course_with_ball();
    REQUIRE(course.ball.has_value());
    check(course);
}
}

TEST_CASE("a tap of F at my ball steps up to it and never picks it up, on the hub and on a course") {
    for_both_places([](game_state& state) {
        const auto near_ball = nearest_interactable(state);
        REQUIRE(near_ball.has_value());
        CHECK(near_ball->kind == interactable_kind::ball);
        tap_interact(state);
        CHECK(state.mode == game_mode::addressing);
        CHECK(state.ball.has_value());
    });
}

TEST_CASE("Space beside my ball jumps and never addresses it, on the hub and on a course") {
    for_both_places([](game_state& state) {
        update_game(state, game_input_from_keys([] { input_state keys; keys.space.pressed = true; keys.space.is_down = true; return keys; }(),
                                                0.0f, state.tuning.setup), 0.016f);
        CHECK(state.mode == game_mode::walking);
        CHECK(!state.player.vertical.grounded);
        CHECK(state.ball.has_value());
    });
}

TEST_CASE("a tap of F lifts a course cup ball immediately") {
    game_state state = course_with_ball();
    put_ball_in_cup(state);
    const glm::vec3 position = state.player.position;
    game_input press; press.interact = true; press.interact_held = true;
    update_game(state, press, 0.016f);
    CHECK(!state.ball.has_value());
    CHECK(!state.ball_holding.has_value());
    CHECK(state.mode == game_mode::walking);
    CHECK(state.player.position == position);
}

TEST_CASE("a tap of F lifts a hub cup ball immediately") {
    game_state state = hub_with_ball();
    put_ball_in_cup(state);
    tap_interact(state);
    CHECK(!state.ball.has_value());
    CHECK(!state.ball_holding.has_value());
    CHECK(state.mode == game_mode::walking);
}

TEST_CASE("F held at a resting ball lifts a practice ball only: a course ball stays in play") {
    game_state hub = hub_with_ball();
    hold_interact(hub, hold_seconds(hub));
    CHECK(!hub.ball.has_value());
    CHECK(hub.mode == game_mode::walking);
    game_state course = course_with_ball();
    hold_interact(course, hold_seconds(course));
    CHECK(course.ball.has_value());
    CHECK(course.ball->place != ball_place::in_cup);
}

TEST_CASE("a held F lifts the ball once, then the release does not step up to anything") {
    game_state state = hub_with_ball();
    hold_interact(state, hold_seconds(state));
    REQUIRE(!state.ball.has_value());
    CHECK(state.mode == game_mode::walking);
    CHECK(!state.ball_holding.has_value());
}

TEST_CASE("the same swing strikes my ball once and it comes to rest, on the hub and on a course") {
    for_both_places([](game_state& state) {
        hit_selected_club(state);
        REQUIRE(state.shot.has_value());
        CHECK(state.ball->strokes == 1);
        for (int frame = 0; frame < 4000 && state.shot; ++frame) { update_game(state, game_input{}, 0.016f); }
        CHECK(!state.shot.has_value());
        REQUIRE(state.ball.has_value());
        CHECK(state.ball->strokes == 1);
        CHECK(state.ball->place != ball_place::in_cup);
        CHECK(state.mode == game_mode::walking);
    });
}

TEST_CASE("holing out puts the ball in its cup on the hub and on a course; only a course scores the hole") {
    for_both_places([](game_state& state) {
        hit_selected_club(state);
        REQUIRE(state.shot.has_value());
        state.shot->result.holed = true;
        for (int frame = 0; frame < 4000 && state.shot; ++frame) { update_game(state, game_input{}, 0.016f); }
        REQUIRE(state.ball.has_value());
        CHECK(state.ball->place == ball_place::in_cup);
        CHECK(state.mode == game_mode::walking);
        CHECK(!state.shot.has_value());
        CHECK(state.round.has_value() == state.place.rules.scored);
        if (state.round) { CHECK(state.round->strokes.front() == 1); }
    });
}

TEST_CASE("a tap lifts a holed spare while leaving the ball in play") {
    auto state = hub_with_ball();
    state.spare_balls.push_back({42, 0, state.player.position, true, 1});
    state.ball->position += glm::vec3(5,0,0);
    tap_interact(state);
    CHECK(state.spare_balls.empty());
    CHECK(state.ball.has_value());
    CHECK(!state.ball_holding.has_value());
}
