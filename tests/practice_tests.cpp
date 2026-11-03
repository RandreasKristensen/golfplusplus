#include "doctest.h"

#include "game/address_play.h"
#include "game/course_session.h"
#include "game/golf_bag.h"
#include "game/golf_cart.h"
#include "game/interactables.h"
#include "game/save_data.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <glm/trigonometric.hpp>

namespace {
// The fixture course played as a practice ground with no carts.
game_state practice_game() {
    course_definition course = fixture_hub_course();
    course.practice = true;
    course.carts = false;
    return started_game(course);
}

game_input bag_input() {
    game_input input;
    input.move_bag = true;
    return input;
}

// F, then looking down at the ball the swing, played until it rests.
void hit(game_state& state) {
    state.address.look = down_look(state.tuning.camera);
    start_swing_on_time(state);
    swing_through(state);
    for (int frame = 0; frame < 2000 && shot_playing(state); ++frame) {
        update_game(state, game_input{}, 0.05f);
    }
}
}

TEST_CASE("a practice ground without carts starts with my bag on my back, and no cart of mine anywhere") {
    game_state state = practice_game();
    CHECK(!own_cart(state));
    CHECK(bag_on_back(state));
    CHECK(carts_in_world(state).empty());
    CHECK(!seat_door_at(state));
    // B sets it down at my feet, where its picker is in reach.
    update_game(state, bag_input(), 0.016f);
    finish_handling(state);
    CHECK(!bag_on_back(state));
    CHECK(bag_in_reach(state).has_value());
}

TEST_CASE("practising, F after a shot drops a fresh ball where I stand, set up as the last, with nothing scored or earned") {
    game_state state = practice_game();
    update_game(state, bag_input(), 0.016f);
    finish_handling(state);
    update_game(state, interact_input(), 0.016f);
    REQUIRE(state.mode == game_mode::addressing);
    const glm::vec3 tee = state.ball->position;
    // A setup of my own: turned, opened, a shorter swing.
    state.address.setup.aim_angle = wrap_angle(state.address.setup.aim_angle + glm::radians(3.0f));
    state.address.setup.face_open = glm::radians(2.0f);
    state.address.setup.backswing = 0.6f;
    const std::string progress_before = save_data_to_json(state.save);

    hit(state);
    REQUIRE(state.ball->strokes == 1);
    CHECK(horizontal_distance(state.ball->position, tee) > 1.0f);
    // Where the swing left me, the station is the nearest thing F acts on.
    const glm::vec3 stood = state.player.position;
    CHECK(horizontal_distance(stood, tee) < state.tuning.player.ball_interact_radius);
    const std::optional<interactable> nearest = nearest_interactable(state);
    REQUIRE(nearest.has_value());
    CHECK(nearest->kind == interactable_kind::tee);
    CHECK(save_data_to_json(state.save) == progress_before);
    CHECK(!hole_played(state.round, 0));

    const shot_setup last = state.address.setup;
    update_game(state, interact_input(), 0.016f);
    REQUIRE(state.mode == game_mode::addressing);
    CHECK(state.ball->strokes == 0);
    CHECK(near(state.ball->position, tee));
    CHECK(near(state.address.setup.aim_angle, last.aim_angle, 0.0001f));
    CHECK(near(state.address.setup.face_open, last.face_open, 0.0001f));
    CHECK(near(state.address.setup.backswing, last.backswing, 0.0001f));
    CHECK(looking_down(state.address.look, state.tuning.camera));
    // I stand where I stood: nothing to walk.
    CHECK(horizontal_distance(state.player.position, stood) < 0.01f);
}

TEST_CASE("practising, another station takes my ball at once, no give-up question, and a holed ball needs no picking up") {
    game_state state = practice_game();
    update_game(state, bag_input(), 0.016f);
    finish_handling(state);
    update_game(state, interact_input(), 0.016f);
    hit(state);
    REQUIRE(ball_in_play(state));

    state.player.position = state.world.hole_starts[1].position;
    update_game(state, interact_input(), 0.016f);
    CHECK(!state.give_up_prompt);
    REQUIRE(state.ball.has_value());
    CHECK(state.ball->hole == 1U);
    CHECK(state.ball->strokes == 0);

    // Holed: it lies in the cup, nothing is completed, and F drops another.
    state.mode = game_mode::walking;
    const glm::vec3 cup = state.area.cups[1].centre;
    play_shot(state, shot_result{cup, true, 0.05f, {state.ball->position, cup}, {}});
    for (int i = 0; i < 20 && ball_in_play(state); ++i) {
        update_game(state, game_input{}, 0.02f);
    }
    REQUIRE(state.ball->place == ball_place::in_cup);
    CHECK(!hole_played(state.round, 1));
    state.player.position = state.world.hole_starts[1].position;
    update_game(state, interact_input(), 0.016f);
    CHECK(state.ball->place == ball_place::teed);
}

TEST_CASE("practising, Tab shows no scorecard") {
    game_state state = practice_game();
    game_input tab;
    tab.scorecard_held = true;
    update_game(state, tab, 0.016f);
    CHECK(!state.scorecard_active);
}
