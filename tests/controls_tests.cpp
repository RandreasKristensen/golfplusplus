#include "doctest.h"

#include "core/key_bindings.h"
#include "core/render_frame.h"
#include "game/address_play.h"
#include "game/area_solids.h"
#include "game/game_state.h"
#include "game/golf_cart.h"
#include "game/player_jump.h"
#include "game/rangefinder.h"
#include "game/tee_box.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

namespace {
// The right button's drag threshold the tests bind with.
constexpr int drag = 6;

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
    CHECK(game_input_from_keys(held(&input_state::key_w), 0.0f, drag).forward_held);
    CHECK(game_input_from_keys(held(&input_state::key_s), 0.0f, drag).back_held);
    CHECK(game_input_from_keys(held(&input_state::key_a), 0.0f, drag).strafe_left_held);
    CHECK(game_input_from_keys(held(&input_state::key_d), 0.0f, drag).strafe_right_held);
    CHECK(game_input_from_keys(held(&input_state::left), 0.0f, drag).turn_left_held);
    CHECK(game_input_from_keys(held(&input_state::right), 0.0f, drag).turn_right_held);
    CHECK(game_input_from_keys(held(&input_state::up), 0.0f, drag).look_up_held);
    CHECK(game_input_from_keys(held(&input_state::down), 0.0f, drag).look_down_held);
    CHECK(game_input_from_keys(held(&input_state::key_m), 0.0f, drag).course_map_held);
    // B moves the bag; a left click takes a club in the picker.
    CHECK(game_input_from_keys(held(&input_state::key_b), 0.0f, drag).move_bag);
    CHECK(game_input_from_keys(held(&input_state::mouse_left), 0.0f, drag).take_club);

    const game_input enter = game_input_from_keys(held(&input_state::enter), 0.0f, drag);
    CHECK(!enter.course_map_held);
    CHECK(!enter.interact);
    CHECK(!enter.swing);
    CHECK(!enter.jump);
}

TEST_CASE("the picker steps up the bag by W, up and the wheel away from me, down by S, down and the wheel back; walking the wheel changes nothing") {
    CHECK(game_input_from_keys(held(&input_state::key_w), 0.0f, drag).picker_steps == 1);
    CHECK(game_input_from_keys(held(&input_state::up), 0.0f, drag).picker_steps == 1);
    CHECK(game_input_from_keys(held(&input_state::key_s), 0.0f, drag).picker_steps == -1);
    CHECK(game_input_from_keys(held(&input_state::down), 0.0f, drag).picker_steps == -1);
    input_state keys;
    keys.wheel_steps = 2;
    CHECK(game_input_from_keys(keys, 0.0f, drag).picker_steps == 2);
    keys.wheel_steps = -1;
    CHECK(game_input_from_keys(keys, 0.0f, drag).picker_steps == -1);
    CHECK(next_frame_input(keys).wheel_steps == 0);

    game_state state = started_hole();
    state.selected_club = 2;
    input_state notch;
    notch.wheel_steps = 1;
    update_game(state, game_input_from_keys(notch, 0.0f, drag), 0.016f);
    CHECK(state.selected_club == 2);

    enter_addressing(state);
    REQUIRE(state.mode == game_mode::addressing);
    update_game(state, game_input_from_keys(notch, 0.0f, drag), 0.016f);
    CHECK(state.selected_club == 2);
}

TEST_CASE("F steps up to the ball and away; Space swings over it and jumps walking") {
    const game_input f = game_input_from_keys(held(&input_state::key_f), 0.0f, drag);
    CHECK(f.interact);
    CHECK(!f.swing);
    const game_input space = game_input_from_keys(held(&input_state::space), 0.0f, drag);
    CHECK(space.swing);
    CHECK(space.jump);
    CHECK(!space.interact);
    // E and Q no longer swing, and a left click no longer addresses the ball.
    CHECK(!game_input_from_keys(held(&input_state::key_e), 0.0f, drag).swing);
    CHECK(!game_input_from_keys(held(&input_state::key_q), 0.0f, drag).swing);
    const game_input click = game_input_from_keys(held(&input_state::mouse_left), 0.0f, drag);
    CHECK(!click.interact);
    CHECK(!click.swing);

    // Space beside the ball does not take it up; F does, and F again steps away.
    game_state state = started_hole();
    update_game(state, game_input_from_keys(held(&input_state::space), 0.0f, drag), 0.016f);
    CHECK(state.mode == game_mode::walking);
    update_game(state, game_input_from_keys(held(&input_state::mouse_left), 0.0f, drag), 0.016f);
    CHECK(state.mode == game_mode::walking);
    update_game(state, game_input_from_keys(held(&input_state::key_f), 0.0f, drag), 0.016f);
    REQUIRE(state.mode == game_mode::addressing);
    update_game(state, game_input_from_keys(held(&input_state::key_f), 0.0f, drag), 0.016f);
    CHECK(state.mode == game_mode::walking);
    update_game(state, game_input_from_keys(held(&input_state::key_f), 0.0f, drag), 0.016f);
    REQUIRE(state.mode == game_mode::addressing);

    // Space swings, and the ball is struck at impact.
    update_game(state, game_input_from_keys(held(&input_state::space), 0.0f, drag), 0.016f);
    REQUIRE(state.address.swing_elapsed.has_value());
    CHECK(state.ball->strokes == 0);
    swing_through(state);
    CHECK(state.ball->strokes == 1);
    CHECK(shot_playing(state));
}

TEST_CASE("over the ball Q/E turn the face, Z/C move the ball, W/S the backswing and A/D the stance, each its own way") {
    game_state state = started_hole();
    enter_addressing(state);
    const shot_setup stock = state.address.setup;
    const setup_tuning& tuning = state.tuning.setup;
    const auto press = [&](button_state input_state::*key) {
        update_game(state, game_input_from_keys(held(key), 0.0f, drag), 0.016f);
        update_game(state, game_input{}, 0.016f);
    };
    press(&input_state::key_q);
    CHECK(near(state.address.setup.face_open, stock.face_open + glm::radians(tuning.face_step_degrees), 0.0001f));
    press(&input_state::key_e);
    press(&input_state::key_e);
    CHECK(near(state.address.setup.face_open, stock.face_open - glm::radians(tuning.face_step_degrees), 0.0001f));
    press(&input_state::key_c);
    CHECK(near(state.address.setup.ball_position, stock.ball_position + tuning.ball_position_step, 0.0001f));
    press(&input_state::key_z);
    press(&input_state::key_z);
    CHECK(near(state.address.setup.ball_position, stock.ball_position - tuning.ball_position_step, 0.0001f));
    press(&input_state::key_s);
    CHECK(state.address.setup.backswing < stock.backswing);
    press(&input_state::key_w);
    press(&input_state::key_w);
    CHECK(state.address.setup.backswing > stock.backswing);
    press(&input_state::key_a);
    CHECK(near(wrap_angle(state.address.setup.aim_angle - stock.aim_angle), glm::radians(tuning.aim_step_degrees), 0.0001f));
    press(&input_state::right);
    press(&input_state::right);
    CHECK(near(wrap_angle(state.address.setup.aim_angle - stock.aim_angle), -glm::radians(tuning.aim_step_degrees), 0.0001f));
    // None of them walks me away from the ball, and R puts the stock setup back, keeping the aim.
    CHECK(state.mode == game_mode::addressing);
    const float aim = state.address.setup.aim_angle;
    press(&input_state::key_r);
    CHECK(state.address.setup.face_open == stock.face_open);
    CHECK(state.address.setup.ball_position == stock.ball_position);
    CHECK(state.address.setup.backswing == stock.backswing);
    CHECK(state.address.setup.aim_angle == aim);
    // My stance follows the setup.
    CHECK(near(state.player.position, address_stance_position(state.area, state.address.setup, state.tuning), 0.0001f));
}

TEST_CASE("every mouse action has a key that does the same") {
    input_state wheel_up;
    wheel_up.wheel_steps = 1;
    // Shift, Ctrl and Alt with the wheel turn the stance, the face and the ball, as A, Q and C do.
    input_state shift = wheel_up;
    shift.left_shift.is_down = true;
    CHECK(game_input_from_keys(shift, 0.0f, drag).aim_steps == 1);
    input_state ctrl = wheel_up;
    ctrl.ctrl.is_down = true;
    CHECK(game_input_from_keys(ctrl, 0.0f, drag).face_steps == 1);
    input_state alt = wheel_up;
    alt.alt.is_down = true;
    CHECK(game_input_from_keys(alt, 0.0f, drag).ball_steps == 1);
    CHECK(game_input_from_keys(wheel_up, 0.0f, drag).look_target_wheel_steps == 1);
    CHECK(game_input_from_keys(shift, 0.0f, drag).look_target_wheel_steps == 0);
    // Middle-click resets as R does; a right-click aims at the view as T does.
    CHECK(game_input_from_keys(held(&input_state::mouse_middle), 0.0f, drag).reset_setup);
    CHECK(game_input_from_keys(held(&input_state::key_r), 0.0f, drag).reset_setup);
    CHECK(game_input_from_keys(held(&input_state::key_t), 0.0f, drag).aim_at_view);
    input_state right_click;
    right_click.mouse_right.released = true;
    CHECK(game_input_from_keys(right_click, 0.0f, drag).aim_at_view);
    CHECK(game_input_from_keys(held(&input_state::key_v), 0.0f, drag).snap_view);

    // The same steps whichever way they come.
    game_state by_key = started_hole();
    game_state by_wheel = started_hole();
    enter_addressing(by_key);
    enter_addressing(by_wheel);
    for (const auto& [key, wheel] : {std::pair{&input_state::key_a, shift}, std::pair{&input_state::key_q, ctrl},
                                     std::pair{&input_state::key_c, alt}}) {
        update_game(by_key, game_input_from_keys(held(key), 0.0f, drag), 0.016f);
        update_game(by_key, game_input{}, 0.016f);
        update_game(by_wheel, game_input_from_keys(wheel, 0.0f, drag), 0.016f);
    }
    CHECK(near(by_key.address.setup.aim_angle, by_wheel.address.setup.aim_angle, 0.0001f));
    CHECK(near(by_key.address.setup.face_open, by_wheel.address.setup.face_open, 0.0001f));
    CHECK(near(by_key.address.setup.ball_position, by_wheel.address.setup.ball_position, 0.0001f));
}

TEST_CASE("a right-drag past the threshold turns the stance and never also aims; a click aims") {
    input_state keys;
    keys.mouse_right.is_down = true;
    keys.mouse_dx = 4;
    keys.right_drag_counts = drag;
    const game_input small = game_input_from_keys(keys, 0.01f, drag);
    CHECK(small.stance_drag_yaw == 0.0f);
    CHECK(near(small.look_yaw, -0.04f));
    keys.right_drag_counts = drag + 1;
    const game_input dragging = game_input_from_keys(keys, 0.01f, drag);
    CHECK(near(dragging.stance_drag_yaw, -0.04f));
    CHECK(dragging.look_yaw == 0.0f);
    CHECK(dragging.look_pitch == 0.0f);
    CHECK(!dragging.aim_at_view);
    // Let go after a drag: no aim.
    keys.mouse_right.is_down = false;
    keys.mouse_right.released = true;
    keys.mouse_dx = 0;
    CHECK(!game_input_from_keys(keys, 0.01f, drag).aim_at_view);
    keys.right_drag_counts = 0;
    CHECK(game_input_from_keys(keys, 0.01f, drag).aim_at_view);
    // A drag turns the stance by its yaw, and the look not at all.
    game_state state = started_hole();
    enter_addressing(state);
    const shot_setup before = state.address.setup;
    const address_look look = state.address.look;
    game_input turn;
    turn.stance_drag_yaw = 0.02f;
    update_game(state, turn, 0.016f);
    CHECK(near(wrap_angle(state.address.setup.aim_angle - before.aim_angle), 0.02f, 0.0001f));
    CHECK(state.address.look.yaw == look.yaw);
}

TEST_CASE("right-click aims the path at the spot under the view and keeps the shape") {
    game_state state = started_hole();
    enter_addressing(state);
    state.address.setup.face_open = glm::radians(6.0f);
    const float shape = state.address.setup.face_open;
    // Look off to the side, along the ground some way out.
    state.address.look = clamped_look(address_look{glm::radians(60.0f), glm::radians(-8.0f)}, state.tuning.camera);
    const eye_view eye = my_address_eye(state);
    const std::optional<glm::vec3> spot = ground_hit(state.area, eye.position, eye.direction, state.tuning.rangefinder,
                                                     state.tuning.scale);
    REQUIRE(spot.has_value());
    game_input aim;
    aim.aim_at_view = true;
    update_game(state, aim, 0.016f);
    CHECK(near(wrap_angle(state.address.setup.aim_angle - yaw_towards(state.address.setup.ball_start, *spot)), 0.0f, 0.0001f));
    CHECK(state.address.setup.face_open == shape);
}

TEST_CASE("look-and-scroll picks the part under the view's centre, and the wheel turns it") {
    game_state state = started_hole();
    enter_addressing(state);
    const address_marks marks = my_address_marks(state);
    const eye_view eye = my_address_eye(state);
    const auto look_at = [&](const glm::vec3& point) {
        const glm::vec3 to = point - eye.position;
        const float yaw = wrap_angle(yaw_towards(eye.position, point) - address_facing_yaw(state.address.setup));
        const float pitch = std::atan2(to.y, glm::length(horizontal(to)));
        state.address.look = address_look{hand_sign(state.address.setup.hand) * yaw, pitch};
        REQUIRE(near(my_address_eye(state).direction, glm::normalize(to), 0.001f));
    };
    look_at(marks.ball);
    CHECK(my_pointed_part(state) == setup_part::ball);
    look_at(0.5f * (marks.face_heel + marks.face_toe));
    CHECK(my_pointed_part(state) == setup_part::face);
    look_at(marks.stick_front);
    CHECK(my_pointed_part(state) == setup_part::stance);
    state.address.look = target_look(state.tuning.camera);
    CHECK(my_pointed_part(state) == setup_part::none);

    // Looking at the face, a notch opens it; at the sky, it lengthens the backswing.
    look_at(0.5f * (marks.face_heel + marks.face_toe));
    const shot_setup before = state.address.setup;
    game_input notch;
    notch.look_target_wheel_steps = 1;
    update_game(state, notch, 0.016f);
    CHECK(state.address.setup.face_open > before.face_open);
    CHECK(state.address.setup.backswing == before.backswing);
    state.address.look = target_look(state.tuning.camera);
    notch.look_target_wheel_steps = -1;
    update_game(state, notch, 0.016f);
    CHECK(state.address.setup.backswing < before.backswing);
}

TEST_CASE("V turns my head to the other framed view over the snap time, with no frame where the view jumps") {
    game_state state = started_hole();
    enter_addressing(state);
    const camera_tuning& camera = state.tuning.camera;
    const address_look target = target_look(camera);
    const address_look down = down_look(camera);
    REQUIRE(near(state.address.look.yaw, target.yaw, 0.0001f));
    game_input v;
    v.snap_view = true;
    update_game(state, v, 0.016f);
    // The turn's length in yaw and pitch: no frame turns the view more than an eased share of it.
    const float total = std::abs(target.yaw - down.yaw) + std::abs(target.pitch - down.pitch);
    constexpr float dt = 1.0f / 60.0f;
    float elapsed = 0.0f;
    glm::vec3 before = my_address_eye(state).direction;
    // The mouse waits while it turns.
    game_input mouse;
    mouse.look_yaw = 0.3f;
    while (state.address.snap) {
        update_game(state, mouse, dt);
        elapsed += dt;
        const glm::vec3 now = my_address_eye(state).direction;
        // Never more than a smooth, eased frame's share of the turn.
        CHECK(std::acos(std::clamp(glm::dot(now, before), -1.0f, 1.0f)) < 1.6f * total * dt / camera.address_snap_seconds);
        before = now;
        REQUIRE(elapsed < 2.0f);
    }
    CHECK(near(elapsed, camera.address_snap_seconds, 2.0f * dt));
    CHECK(near(state.address.look.yaw, down.yaw, 0.0001f));
    CHECK(near(state.address.look.pitch, down.pitch, 0.0001f));
    // And back.
    update_game(state, v, dt);
    for (int i = 0; i < 60 && state.address.snap; ++i) {
        update_game(state, game_input{}, dt);
    }
    CHECK(near(state.address.look.yaw, target.yaw, 0.0001f));
}

TEST_CASE("Space at the wheel does not jump or step out of the cart; F steps out") {
    game_state state = started_hole();
    sit_in_cart(state);
    update_game(state, game_input_from_keys(held(&input_state::space), 0.0f, drag), 0.016f);
    CHECK(driving_cart(state));
    CHECK(state.player.vertical.grounded);
    update_game(state, game_input_from_keys(held(&input_state::key_f), 0.0f, drag), 0.016f);
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
    REQUIRE(!state.player.vertical.grounded);
    CHECK(state.player.position.y > terrain_height(state.area, state.player.position));

    // Space again in the air does not jump again; W walks on at walking speed.
    float highest = 0.0f;
    float walked = 0.0f;
    float elapsed = 0.02f;
    for (; !state.player.vertical.grounded && elapsed < 5.0f; elapsed += 0.02f) {
        const float before = state.player.vertical.speed;
        const glm::vec3 from = state.player.position;
        update_game(state, jump, 0.02f);
        CHECK((state.player.vertical.grounded || state.player.vertical.speed < before));
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
    const player_tuning& tuning = fine.tuning.player;
    CHECK(near(fine.player.position.y - terrain_height(fine.area, fine.player.position),
               tuning.jump_speed * 0.1f - 0.5f * tuning.jump_gravity * 0.01f, 0.0001f));
}

TEST_CASE("no jump over the ball, and stepping up to it mid-air lands me") {
    game_state state = started_hole();
    game_input jump;
    jump.jump = true;
    update_game(state, jump, 0.05f);
    REQUIRE(!state.player.vertical.grounded);
    update_game(state, interact_input(), 0.016f);
    REQUIRE(state.mode == game_mode::addressing);
    CHECK(state.player.vertical.grounded);
    CHECK(near(state.player.position.y, terrain_height(state.area, state.player.position)));

    const float addressing = state.player.position.y;
    update_game(state, jump, 0.05f);
    CHECK(state.player.vertical.grounded);
    CHECK(state.player.position.y == addressing);
}

TEST_CASE("the mouse turns the look by its counts: right and down turn right and down") {
    input_state keys;
    keys.mouse_dx = 10;
    keys.mouse_dy = -4;
    const game_input input = game_input_from_keys(keys, 0.01f, drag);
    CHECK(near(input.look_yaw, -0.1f));
    CHECK(near(input.look_pitch, 0.04f));

    const game_input uncaptured = game_input_from_keys(keys, 0.0f, drag);
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

TEST_CASE("over the ball the mouse and up and down look freely within the clamp, never turning the stance") {
    game_state state = started_hole();
    enter_addressing(state);
    const camera_tuning& camera = state.tuning.camera;
    const float aim = state.address.setup.aim_angle;
    const address_look start = state.address.look;

    // The mouse left turns a right-hander's head towards the target.
    game_input mouse;
    mouse.look_yaw = 0.1f;
    mouse.look_pitch = -0.05f;
    update_game(state, mouse, 0.016f);
    CHECK(near(state.address.look.yaw, std::min(start.yaw + 0.1f, glm::radians(camera.address_max_yaw_degrees)), 0.0001f));
    CHECK(near(state.address.look.pitch, start.pitch - 0.05f, 0.0001f));
    CHECK(state.address.setup.aim_angle == aim);

    game_input down;
    down.look_down_held = true;
    for (int i = 0; i < 200; ++i) {
        update_game(state, down, 0.05f);
    }
    CHECK(near(state.address.look.pitch, glm::radians(camera.address_min_pitch_degrees), 0.0001f));
    mouse.look_yaw = 50.0f;
    update_game(state, mouse, 0.016f);
    CHECK(near(state.address.look.yaw, glm::radians(camera.address_max_yaw_degrees), 0.0001f));
    CHECK(state.address.setup.aim_angle == aim);
    // The look stays where I leave it when the stance turns: it turns with it.
    game_input turn;
    turn.strafe_left_held = true;
    const float view_before = address_view_yaw(state.address.setup, state.address.look);
    update_game(state, turn, 0.016f);
    const float turned = wrap_angle(state.address.setup.aim_angle - aim);
    CHECK(turned > 0.0f);
    CHECK(near(wrap_angle(address_view_yaw(state.address.setup, state.address.look) - view_before), turned, 0.0001f));
}

TEST_CASE("jumping onto a tee box the hop keeps its arc over the box's edge and lands on its top") {
    game_state state = started_hole();
    const player_tuning& tuning = state.tuning.player;
    const glm::vec3 ahead = yaw_direction(state.player.yaw);
    tee_box box;
    box.center = state.player.position + ahead * 3.5f + glm::vec3(0.0f, 0.3f, 0.0f);
    box.down_hole = ahead;
    box.half_length = 1.5f;
    box.half_width = 2.0f;
    box.bottom = state.player.position.y - 1.0f;
    state.area.tee_boxes = {box};
    set_solids(state.area, tee_box_solids(state.area.tee_boxes, state.tuning));
    const float takeoff = state.player.position.y;
    const auto rise = [&tuning](const float seconds) {
        return tuning.jump_speed * seconds - 0.5f * tuning.jump_gravity * seconds * seconds;
    };

    game_input jump;
    jump.jump = true;
    jump.forward_held = true;
    update_game(state, jump, 0.02f);
    float elapsed = 0.02f;
    bool crossed = false;
    for (; !state.player.vertical.grounded && elapsed < 5.0f; elapsed += 0.02f) {
        CHECK(near(state.player.position.y, takeoff + rise(elapsed), 0.0001f));
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

TEST_CASE("P on a green in reach of its cup takes the flag out and puts it back, offline only") {
    game_state state = started_hole();
    const std::size_t hole = state.ball->hole;
    const cup& target = state.area.cups[hole];
    state.player.position = anchor_on_terrain(state.area, target.centre + glm::vec3(1.0f, 0.0f, 0.0f));
    REQUIRE(sample_area(state.area, state.player.position).material == terrain_material::green);
    REQUIRE(state.flags_in[hole]);
    game_input p;
    p.toggle_flag = true;
    update_game(state, p, 0.016f);
    CHECK(!state.flags_in[hole]);
    update_game(state, p, 0.016f);
    CHECK(state.flags_in[hole]);
    // Out of reach, nothing.
    state.player.position = anchor_on_terrain(state.area, target.centre + glm::vec3(state.tuning.player.flag_reach + 1.0f, 0.0f, 0.0f));
    update_game(state, p, 0.016f);
    CHECK(state.flags_in[hole]);
    // Online every flag stays in.
    state.play = play_mode::online;
    state.player.position = anchor_on_terrain(state.area, target.centre + glm::vec3(1.0f, 0.0f, 0.0f));
    update_game(state, p, 0.016f);
    CHECK(state.flags_in[hole]);
}
