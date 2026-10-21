#include "doctest.h"

#include "core/key_bindings.h"
#include "core/render_frame.h"
#include "game/game_state.h"
#include "game/golf_cart.h"
#include "game/player_jump.h"
#include "game/tee_box.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

namespace {
input_state held(button_state input_state::*key) {
    input_state keys;
    (keys.*key).is_down = true;
    (keys.*key).pressed = true;
    return keys;
}

// Where one frame of `input` moved me, on the ground plane.
glm::vec3 step_of(game_state& state, const game_input& input, const float dt) {
    const glm::vec3 before = state.player.position;
    update_game(state, input, dt);
    return horizontal(state.player.position - before);
}
}

TEST_CASE("WASD walk, the arrow keys look, M holds up the course map and Enter does nothing in play") {
    CHECK(game_input_from_keys(held(&input_state::key_w), 0.0f).forward_held);
    CHECK(game_input_from_keys(held(&input_state::key_s), 0.0f).back_held);
    CHECK(game_input_from_keys(held(&input_state::key_a), 0.0f).strafe_left_held);
    CHECK(game_input_from_keys(held(&input_state::key_d), 0.0f).strafe_right_held);
    CHECK(game_input_from_keys(held(&input_state::left), 0.0f).turn_left_held);
    CHECK(game_input_from_keys(held(&input_state::right), 0.0f).turn_right_held);
    CHECK(game_input_from_keys(held(&input_state::up), 0.0f).look_up_held);
    CHECK(game_input_from_keys(held(&input_state::down), 0.0f).look_down_held);
    CHECK(game_input_from_keys(held(&input_state::key_m), 0.0f).course_map_held);

    const game_input up = game_input_from_keys(held(&input_state::up), 0.0f);
    CHECK(!up.forward_held);
    CHECK(up.club_steps == 1);
    CHECK(game_input_from_keys(held(&input_state::key_w), 0.0f).club_steps == 1);
    CHECK(game_input_from_keys(held(&input_state::key_s), 0.0f).club_steps == -1);
    CHECK(game_input_from_keys(held(&input_state::down), 0.0f).club_steps == -1);

    const game_input enter = game_input_from_keys(held(&input_state::enter), 0.0f);
    CHECK(!enter.course_map_held);
    CHECK(!enter.interact);
    CHECK(!enter.backswing);
    CHECK(!enter.downswing);
    CHECK(!enter.jump);
}

TEST_CASE("the wheel changes club a notch a club, away from me longer like W and up") {
    input_state keys;
    keys.wheel_steps = 2;
    CHECK(game_input_from_keys(keys, 0.0f).club_steps == 2);
    keys.wheel_steps = -1;
    CHECK(game_input_from_keys(keys, 0.0f).club_steps == -1);
    CHECK(next_frame_input(keys).wheel_steps == 0);
    input_state w_and_wheel = held(&input_state::key_w);
    w_and_wheel.wheel_steps = -1;
    CHECK(game_input_from_keys(w_and_wheel, 0.0f).club_steps == 0);

    // Walking on foot only the wheel changes club, as W and S walk.
    game_state state = started_hole();
    input_state notch;
    notch.wheel_steps = 1;
    update_game(state, game_input_from_keys(notch, 0.0f), 0.016f);
    CHECK(state.selected_club == 1);
    update_game(state, game_input_from_keys(held(&input_state::key_w), 0.0f), 0.016f);
    CHECK(state.selected_club == 1);
    state.selected_club = 0;

    // And wherever W and S change club: aiming and over the ball.
    enter_aiming(state);
    update_game(state, game_input_from_keys(notch, 0.0f), 0.016f);
    CHECK(state.selected_club == 1);
    update_game(state, interact_input(), 0.016f);
    REQUIRE(state.mode == game_mode::addressing);
    update_game(state, game_input_from_keys(notch, 0.0f), 0.016f);
    CHECK(state.selected_club == 2 % state.clubs.size());
}

TEST_CASE("F takes up the ball and steps up to it, E starts the backswing and Q hits; Space only jumps") {
    const game_input f = game_input_from_keys(held(&input_state::key_f), 0.0f);
    CHECK(f.interact);
    CHECK(!f.backswing);
    CHECK(!f.jump);
    const game_input space = game_input_from_keys(held(&input_state::space), 0.0f);
    CHECK(space.jump);
    CHECK(!space.interact);
    CHECK(!space.backswing);
    CHECK(!space.downswing);
    const game_input e = game_input_from_keys(held(&input_state::key_e), 0.0f);
    CHECK(e.backswing);
    CHECK(!e.downswing);
    const game_input q = game_input_from_keys(held(&input_state::key_q), 0.0f);
    CHECK(q.downswing);
    CHECK(!q.backswing);

    // Space beside the ball does not take it up; F does.
    game_state state = started_hole();
    update_game(state, game_input_from_keys(held(&input_state::space), 0.0f), 0.016f);
    CHECK(state.mode == game_mode::walking);
    update_game(state, game_input_from_keys(held(&input_state::key_f), 0.0f), 0.016f);
    REQUIRE(state.mode == game_mode::aiming);

    // Aiming, D turns the aim and Space does nothing; F steps up to the ball.
    const float aim = state.aim_angle;
    update_game(state, game_input_from_keys(held(&input_state::key_d), 0.0f), 0.016f);
    CHECK(state.mode == game_mode::aiming);
    CHECK(state.aim_angle != aim);
    update_game(state, game_input_from_keys(held(&input_state::space), 0.0f), 0.016f);
    CHECK(state.mode == game_mode::aiming);
    update_game(state, game_input_from_keys(held(&input_state::key_f), 0.0f), 0.016f);
    REQUIRE(state.mode == game_mode::addressing);
    CHECK(state.swing.phase == swing_phase::idle);

    // Over the ball, Q, F and Space before the backswing do nothing.
    for (button_state input_state::*key : {&input_state::key_q, &input_state::key_f, &input_state::space}) {
        update_game(state, game_input_from_keys(held(key), 0.0f), 0.016f);
        CHECK(state.mode == game_mode::addressing);
        CHECK(state.swing.phase == swing_phase::idle);
    }
    update_game(state, game_input_from_keys(held(&input_state::key_e), 0.0f), 0.016f);
    REQUIRE(state.swing.phase == swing_phase::timing);
    update_game(state, game_input_from_keys(held(&input_state::key_e), 0.0f), 0.016f);
    CHECK(state.swing.phase == swing_phase::timing);
    CHECK(state.stroke_count == 0);
    update_game(state, game_input_from_keys(held(&input_state::key_q), 0.0f), 0.016f);
    CHECK(state.stroke_count == 1);
    CHECK(shot_playing(state));
}

TEST_CASE("aiming, a left click steps up to the ball like F; walking, it does nothing") {
    game_state state = started_hole();
    update_game(state, game_input_from_keys(held(&input_state::mouse_left), 0.0f), 0.016f);
    CHECK(state.mode == game_mode::walking);
    update_game(state, game_input_from_keys(held(&input_state::key_f), 0.0f), 0.016f);
    REQUIRE(state.mode == game_mode::aiming);
    update_game(state, game_input_from_keys(held(&input_state::mouse_left), 0.0f), 0.016f);
    CHECK(state.mode == game_mode::addressing);
}

TEST_CASE("Space at the wheel does not jump or step out of the cart; F steps out") {
    game_state state = started_hole();
    sit_in_cart(state);
    update_game(state, game_input_from_keys(held(&input_state::space), 0.0f), 0.016f);
    CHECK(driving_cart(state));
    CHECK(!state.player.hop_elapsed);
    update_game(state, game_input_from_keys(held(&input_state::key_f), 0.0f), 0.016f);
    CHECK(!driving_cart(state));
}

TEST_CASE("a jump rises off the ground and lands back on it, walking on in the air") {
    game_state state = started_hole();
    const player_tuning& tuning = state.tuning.player;
    const float apex = tuning.jump_speed * tuning.jump_speed / (2.0f * tuning.jump_gravity);
    REQUIRE(apex > airborne_height);
    REQUIRE(near(state.player.position.y, terrain_height(state.area, state.player.position)));

    game_input jump;
    jump.jump = true;
    jump.forward_held = true;
    update_game(state, jump, 0.02f);
    REQUIRE(state.player.hop_elapsed.has_value());
    CHECK(state.player.position.y > terrain_height(state.area, state.player.position));

    // Space again in the air does not jump again; W walks on at walking speed.
    float highest = 0.0f;
    float walked = 0.0f;
    float elapsed = 0.02f;
    for (; state.player.hop_elapsed && elapsed < 5.0f; elapsed += 0.02f) {
        const float before = *state.player.hop_elapsed;
        const glm::vec3 from = state.player.position;
        update_game(state, jump, 0.02f);
        CHECK((!state.player.hop_elapsed || *state.player.hop_elapsed > before));
        highest = std::max(highest, state.player.position.y - terrain_height(state.area, state.player.position));
        walked += horizontal_distance(state.player.position, from);
    }
    CHECK(highest <= apex + 0.0001f);
    CHECK(highest > apex * 0.95f);
    CHECK(near(walked, tuning.walk_speed * (elapsed - 0.02f), 0.01f));
    CHECK(near(elapsed, hop_seconds(tuning), 0.03f));
    CHECK(near(state.player.position.y, terrain_height(state.area, state.player.position)));
}

TEST_CASE("a jump is as high at a moment at any frame rate") {
    game_state fine = started_hole();
    game_state coarse = started_hole();
    game_input jump;
    jump.jump = true;
    update_game(fine, jump, 0.01f);
    for (int i = 0; i < 9; ++i) {
        update_game(fine, game_input{}, 0.01f);
    }
    update_game(coarse, jump, 0.05f);
    update_game(coarse, game_input{}, 0.05f);
    CHECK(near(fine.player.position.y, coarse.player.position.y, 0.0001f));
    CHECK(near(fine.player.position.y - terrain_height(fine.area, fine.player.position), hop_rise(0.1f, fine.tuning.player),
               0.0001f));
}

TEST_CASE("no jump aiming or over the ball, and taking up the ball mid-air lands me") {
    game_state state = started_hole();
    game_input jump;
    jump.jump = true;
    update_game(state, jump, 0.05f);
    REQUIRE(state.player.hop_elapsed.has_value());
    update_game(state, interact_input(), 0.016f);
    REQUIRE(state.mode == game_mode::aiming);
    CHECK(!state.player.hop_elapsed);
    CHECK(near(state.player.position.y, terrain_height(state.area, state.player.position)));

    const float standing = state.player.position.y;
    update_game(state, jump, 0.05f);
    CHECK(!state.player.hop_elapsed);
    CHECK(state.player.position.y == standing);
    update_game(state, interact_input(), 0.016f);
    REQUIRE(state.mode == game_mode::addressing);
    const float addressing = state.player.position.y;
    update_game(state, jump, 0.05f);
    CHECK(!state.player.hop_elapsed);
    CHECK(state.player.position.y == addressing);
}

TEST_CASE("the mouse turns the look by its counts: right and down turn right and down") {
    input_state keys;
    keys.mouse_dx = 10;
    keys.mouse_dy = -4;
    const game_input input = game_input_from_keys(keys, 0.01f);
    CHECK(near(input.look_yaw, -0.1f));
    CHECK(near(input.look_pitch, 0.04f));

    const game_input uncaptured = game_input_from_keys(keys, 0.0f);
    CHECK(uncaptured.look_yaw == 0.0f);
    CHECK(uncaptured.look_pitch == 0.0f);
}

TEST_CASE("A and D step sideways without turning, W walks where I look") {
    game_state state = started_hole();
    state.player.yaw = 0.3f;
    const float walk = state.tuning.player.walk_speed * 0.05f;

    game_input left;
    left.strafe_left_held = true;
    const glm::vec3 to_left = step_of(state, left, 0.05f);
    CHECK(near(state.player.yaw, 0.3f));
    CHECK(near(to_left, yaw_left(yaw_direction(0.3f)) * walk, 0.0001f));

    game_input right;
    right.strafe_right_held = true;
    CHECK(near(step_of(state, right, 0.05f), -yaw_left(yaw_direction(0.3f)) * walk, 0.0001f));

    // The mouse turns me a quarter turn right: forward is that way now.
    game_input turn;
    turn.look_yaw = -glm::half_pi<float>();
    update_game(state, turn, 0.016f);
    CHECK(near(state.player.yaw, 0.3f - glm::half_pi<float>(), 0.0001f));
    game_input forward;
    forward.forward_held = true;
    CHECK(near(step_of(state, forward, 0.05f), yaw_direction(state.player.yaw) * walk, 0.0001f));
}

TEST_CASE("walking diagonally is no faster, and backing up keeps facing forward") {
    game_state state = started_hole();
    state.player.yaw = 0.0f;
    const float walk = state.tuning.player.walk_speed * 0.05f;

    game_input diagonal;
    diagonal.forward_held = true;
    diagonal.strafe_right_held = true;
    const glm::vec3 step = step_of(state, diagonal, 0.05f);
    CHECK(near(glm::length(step), walk, 0.0001f));
    CHECK(near(glm::normalize(step), glm::normalize(yaw_direction(0.0f) - yaw_left(yaw_direction(0.0f))), 0.0001f));

    game_input back_left;
    back_left.back_held = true;
    back_left.strafe_left_held = true;
    const glm::vec3 back = step_of(state, back_left, 0.05f);
    CHECK(near(glm::length(back), walk, 0.0001f));
    CHECK(near(glm::normalize(back), glm::normalize(-yaw_direction(0.0f) + yaw_left(yaw_direction(0.0f))), 0.0001f));
    CHECK(state.player.speed < 0.0f);
    CHECK(std::abs(state.player.move_angle) <= glm::half_pi<float>());
    CHECK(near(state.player.yaw, 0.0f));
}

TEST_CASE("the arrow keys and the mouse pitch the walking camera, as far as the tuning lets them") {
    game_state state = started_hole();
    const float max_pitch = state.tuning.player.max_look_pitch;

    game_input up;
    up.look_up_held = true;
    update_game(state, up, 0.05f);
    CHECK(near(state.look.pitch, state.tuning.player.look_pitch_rate * 0.05f, 0.0001f));
    const camera_view view = live_camera_view(state, 60.0f);
    CHECK(view.target.y > view.position.y);

    game_input mouse;
    mouse.look_pitch = -10.0f;
    update_game(state, mouse, 0.016f);
    CHECK(near(state.look.pitch, -max_pitch));
    const glm::vec3 look = glm::normalize(live_camera_view(state, 60.0f).target - live_camera_view(state, 60.0f).position);
    CHECK(near(std::asin(look.y), -max_pitch, 0.0001f));
}

TEST_CASE("aiming turns with the arrow keys, A and D and the mouse, and pitch stays out of it") {
    game_state state = started_hole();
    enter_aiming(state);
    const float start = state.aim_angle;

    game_input strafe;
    strafe.strafe_left_held = true;
    update_game(state, strafe, 0.05f);
    CHECK(near(state.aim_angle, start + state.tuning.player.aim_turn_rate * 0.05f, 0.0001f));

    game_input both;
    both.strafe_left_held = true;
    both.turn_left_held = true;
    update_game(state, both, 0.05f);
    CHECK(near(state.aim_angle, start + state.tuning.player.aim_turn_rate * 0.1f, 0.0001f));

    game_input mouse;
    mouse.look_yaw = -0.05f;
    mouse.look_pitch = 0.5f;
    update_game(state, mouse, 0.016f);
    CHECK(near(state.aim_angle, start + state.tuning.player.aim_turn_rate * 0.1f - 0.05f, 0.0001f));
    CHECK(state.look.pitch == 0.0f);
    CHECK(state.mode == game_mode::aiming);
}

TEST_CASE("jumping onto a tee box the hop keeps its arc over the box's edge and lands on its top") {
    game_state state = started_hole();
    state.area.obstacles = upright_obstacles{};
    state.area.tee_boxes.clear();
    const player_tuning& tuning = state.tuning.player;
    const glm::vec3 ahead = yaw_direction(state.player.yaw);
    tee_box box;
    box.center = state.player.position + ahead * 3.5f + glm::vec3(0.0f, 0.3f, 0.0f);
    box.down_hole = ahead;
    box.half_length = 1.5f;
    box.half_width = 2.0f;
    box.bottom = state.player.position.y - 1.0f;
    state.area.tee_boxes.push_back(box);
    const float takeoff = state.player.position.y;

    game_input jump;
    jump.jump = true;
    jump.forward_held = true;
    update_game(state, jump, 0.02f);
    float elapsed = 0.02f;
    bool crossed = false;
    for (; state.player.hop_elapsed && elapsed < 5.0f; elapsed += 0.02f) {
        CHECK(near(state.player.position.y, takeoff + hop_rise(*state.player.hop_elapsed, tuning), 0.0001f));
        crossed = crossed || on_tee_box(box, state.player.position);
        game_input walk;
        walk.forward_held = true;
        update_game(state, walk, 0.02f);
    }
    CHECK(crossed);
    CHECK(on_tee_box(box, state.player.position));
    CHECK(near(state.player.position.y, box.center.y, 0.0001f));
    // It came down on the box before it would have on the ground.
    CHECK(elapsed < hop_seconds(tuning));
}
