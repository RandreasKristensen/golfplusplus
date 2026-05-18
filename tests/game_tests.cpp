#include "doctest.h"

#include "core/input.h"
#include "game/game_state.h"

#include <glm/geometric.hpp>

TEST_CASE("first space press starts swing timing") {
    game_state state = make_initial_game_state();
    input_state input;
    input.space.pressed = true;
    input.space.is_down = true;

    update_game(state, input, 0.016f);

    CHECK(state.swing.phase == swing_phase::timing);
    CHECK(state.stroke_count == 0);
    CHECK(glm::length(state.ball.velocity) == 0.0f);
}

TEST_CASE("second space press launches selected club shot") {
    game_state state = make_initial_game_state();

    input_state input;
    input.space.pressed = true;
    update_game(state, input, 0.016f);

    input.reset_frame();
    update_game(state, input, 0.35f);

    input.reset_frame();
    input.space.pressed = true;
    input.space.is_down = true;
    update_game(state, input, 0.016f);

    CHECK(state.swing.phase == swing_phase::idle);
    CHECK(state.stroke_count == 1);
    CHECK(glm::length(state.ball.velocity) > 0.0f);
}

TEST_CASE("club selection wraps on button presses") {
    game_state state = make_initial_game_state();

    input_state input;
    input.up.pressed = true;
    update_game(state, input, 0.016f);
    CHECK(state.selected_club == state.clubs.size() - 1);

    input.reset_frame();
    input.down.pressed = true;
    update_game(state, input, 0.016f);
    CHECK(state.selected_club == 0);
}

TEST_CASE("held left and right adjust aim") {
    game_state state = make_initial_game_state();

    input_state input;
    input.left.is_down = true;
    update_game(state, input, 0.25f);
    CHECK(state.aim_angle < 0.0f);

    input.left.is_down = false;
    input.right.is_down = true;
    update_game(state, input, 0.05f);
    update_game(state, input, 0.05f);
    CHECK(state.aim_angle > 0.0f);
}

TEST_CASE("launched ball advances through physics updates") {
    game_state state = make_initial_game_state();

    input_state input;
    input.space.pressed = true;
    update_game(state, input, 0.016f);

    input.reset_frame();
    update_game(state, input, 0.35f);

    input.reset_frame();
    input.space.pressed = true;
    update_game(state, input, 0.016f);

    const glm::vec3 launch_position = state.ball.position;
    input.reset_frame();
    update_game(state, input, 0.016f);

    CHECK(glm::length(state.ball.position - launch_position) > 0.0f);
}
