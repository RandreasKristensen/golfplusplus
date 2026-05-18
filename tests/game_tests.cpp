#include "doctest.h"

#include "core/input.h"
#include "game/game_state.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <cmath>

namespace {
void launch_selected_club(game_state& state) {
    input_state input;
    input.space.pressed = true;
    update_game(state, input, 0.016f);

    input.reset_frame();
    input.space.pressed = true;
    update_game(state, input, 0.016f);
}

float horizontal_speed(const glm::vec3& velocity) {
    return std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
}
}

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

TEST_CASE("sandbox course initializes tee and pin") {
    game_state state = make_initial_game_state();

    CHECK(state.tuning.course.tee_position == glm::vec3(0.0f, 0.0f, 0.0f));
    CHECK(state.tuning.course.pin_position == glm::vec3(0.0f, 0.0f, 80.0f));
    CHECK(state.tuning.course.cup_radius > 0.0f);
    CHECK(state.tuning.course.extent > state.tuning.course.pin_position.z);
    CHECK(state.ball.position == state.tuning.course.tee_position);
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

TEST_CASE("launch tuning changes shot speed") {
    game_state low_power = make_initial_game_state();
    game_state high_power = make_initial_game_state();
    low_power.tuning.min_swing_power = 0.1f;
    high_power.tuning.min_swing_power = 0.8f;

    input_state input;
    input.space.pressed = true;
    update_game(low_power, input, 0.016f);
    update_game(high_power, input, 0.016f);

    input.reset_frame();
    input.space.pressed = true;
    update_game(low_power, input, 0.016f);
    update_game(high_power, input, 0.016f);

    CHECK(glm::length(high_power.ball.velocity) > glm::length(low_power.ball.velocity));
}

TEST_CASE("club loft changes launch angle") {
    game_state putter = make_initial_game_state();
    game_state wedge = make_initial_game_state();
    putter.tuning.min_swing_power = 1.0f;
    wedge.tuning.min_swing_power = 1.0f;
    wedge.selected_club = 1;

    launch_selected_club(putter);
    launch_selected_club(wedge);

    const float putter_rise = putter.ball.velocity.y / horizontal_speed(putter.ball.velocity);
    const float wedge_rise = wedge.ball.velocity.y / horizontal_speed(wedge.ball.velocity);
    CHECK(wedge.tuning.clubs[wedge.selected_club].loft_degrees > putter.tuning.clubs[putter.selected_club].loft_degrees);
    CHECK(wedge_rise > putter_rise);
}

TEST_CASE("stop speed tuning controls moving threshold") {
    ball_state ball;
    ball.velocity = glm::vec3(1.0f, 0.0f, 0.0f);

    game_tuning loose = default_game_tuning();
    loose.stop_speed = 2.0f;
    CHECK(!ball_is_moving(ball, loose));

    game_tuning strict = default_game_tuning();
    strict.stop_speed = 0.5f;
    CHECK(ball_is_moving(ball, strict));
}

TEST_CASE("ground roll friction stops a grounded ball") {
    game_state state = make_initial_game_state();
    state.tuning.physics.drag_coeff = 0.0f;
    state.tuning.wind.base_speed = 0.0f;
    state.tuning.wind.speed_variation = 0.0f;
    state.tuning.ground_restitution = 0.0f;
    state.tuning.ground_roll_friction = 10.0f;
    state.tuning.ground_settle_speed = 10.0f;
    state.tuning.stop_speed = 0.05f;
    state.ball.position = state.tuning.course.tee_position;
    state.ball.velocity = glm::vec3(1.0f, 0.0f, 0.0f);

    input_state input;
    for (int i = 0; i < 8; ++i) {
        update_game(state, input, 0.05f);
    }

    CHECK(horizontal_speed(state.ball.velocity) == 0.0f);
    CHECK(!ball_is_moving(state.ball, state.tuning));
}

TEST_CASE("retee resets ball and cancels swing without adding a stroke") {
    game_state state = make_initial_game_state();

    input_state input;
    input.space.pressed = true;
    update_game(state, input, 0.016f);

    state.ball.position = glm::vec3(5.0f, 1.0f, 6.0f);
    state.ball.velocity = glm::vec3(2.0f, 3.0f, 4.0f);
    state.ball.spin = glm::vec3(1.0f, 0.0f, 0.0f);

    input.reset_frame();
    input.retee.pressed = true;
    update_game(state, input, 0.016f);

    CHECK(state.ball.position == state.tuning.course.tee_position);
    CHECK(glm::length(state.ball.velocity) == 0.0f);
    CHECK(glm::length(state.ball.spin) == 0.0f);
    CHECK(state.swing.phase == swing_phase::idle);
    CHECK(state.stroke_count == 0);
}

TEST_CASE("retee works while ball is moving") {
    game_state state = make_initial_game_state();
    state.ball.position = glm::vec3(12.0f, 2.0f, 30.0f);
    state.ball.velocity = glm::vec3(5.0f, 4.0f, 3.0f);
    state.stroke_count = 1;

    input_state input;
    input.retee.pressed = true;
    update_game(state, input, 0.016f);

    CHECK(state.ball.position == state.tuning.course.tee_position);
    CHECK(glm::length(state.ball.velocity) == 0.0f);
    CHECK(state.stroke_count == 1);
}

TEST_CASE("club selection wraps on button presses") {
    game_state state = make_initial_game_state();

    input_state input;
    input.up.pressed = true;
    update_game(state, input, 0.016f);
    CHECK(state.selected_club == state.tuning.clubs.size() - 1);

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
