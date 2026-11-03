#include "doctest.h"

#include "core/key_bindings.h"
#include "core/render_frame.h"
#include "game/address_play.h"
#include "game/area_solids.h"
#include "game/game_state.h"
#include "game/movement_step.h"
#include "game/golf_cart.h"
#include "game/player_jump.h"
#include "game/rangefinder.h"
#include "game/shot_ability.h"
#include "game/tee_box.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

namespace {
// The setup's numbers the tests bind keys with.
setup_tuning bound_setup() {
    setup_tuning setup;
    setup.drag_threshold_counts = 6;
    setup.pull_counts_per_step = 10;
    setup.swipe_drag_counts = 100;
    setup.swipe_wheel_amount = 2.0f;
    return setup;
}
const setup_tuning bound = bound_setup();

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
    CHECK(game_input_from_keys(held(&input_state::key_w), 0.0f, bound).forward_held);
    CHECK(game_input_from_keys(held(&input_state::key_s), 0.0f, bound).back_held);
    CHECK(game_input_from_keys(held(&input_state::key_a), 0.0f, bound).strafe_left_held);
    CHECK(game_input_from_keys(held(&input_state::key_d), 0.0f, bound).strafe_right_held);
    CHECK(game_input_from_keys(held(&input_state::left), 0.0f, bound).turn_left_held);
    CHECK(game_input_from_keys(held(&input_state::right), 0.0f, bound).turn_right_held);
    CHECK(game_input_from_keys(held(&input_state::up), 0.0f, bound).look_up_held);
    CHECK(game_input_from_keys(held(&input_state::down), 0.0f, bound).look_down_held);
    CHECK(game_input_from_keys(held(&input_state::key_m), 0.0f, bound).course_map_held);
    // B moves the bag; a left click takes a club in the picker.
    CHECK(game_input_from_keys(held(&input_state::key_b), 0.0f, bound).move_bag);
    CHECK(game_input_from_keys(held(&input_state::mouse_left), 0.0f, bound).take_club);

    const game_input enter = game_input_from_keys(held(&input_state::enter), 0.0f, bound);
    CHECK(!enter.course_map_held);
    CHECK(!enter.interact);
    CHECK(!enter.swing);
    CHECK(!enter.jump);
}

TEST_CASE("the picker steps up the bag by W, up and the wheel away from me, down by S, down and the wheel back; walking the wheel changes nothing") {
    CHECK(game_input_from_keys(held(&input_state::key_w), 0.0f, bound).picker_steps == 1);
    CHECK(game_input_from_keys(held(&input_state::up), 0.0f, bound).picker_steps == 1);
    CHECK(game_input_from_keys(held(&input_state::key_s), 0.0f, bound).picker_steps == -1);
    CHECK(game_input_from_keys(held(&input_state::down), 0.0f, bound).picker_steps == -1);
    input_state keys;
    keys.wheel_steps = 2;
    CHECK(game_input_from_keys(keys, 0.0f, bound).picker_steps == 2);
    keys.wheel_steps = -1;
    CHECK(game_input_from_keys(keys, 0.0f, bound).picker_steps == -1);
    CHECK(next_frame_input(keys).wheel_steps == 0);

    game_state state = started_hole();
    state.selected_club = 2;
    input_state notch;
    notch.wheel_steps = 1;
    update_game(state, game_input_from_keys(notch, 0.0f, bound), 0.016f);
    CHECK(state.selected_club == 2);

    enter_addressing(state);
    REQUIRE(state.mode == game_mode::addressing);
    update_game(state, game_input_from_keys(notch, 0.0f, bound), 0.016f);
    CHECK(state.selected_club == 2);
}

TEST_CASE("F steps up to the ball and then looks down at it, never up; Esc steps away; Space swings over it and jumps walking") {
    const game_input f = game_input_from_keys(held(&input_state::key_f), 0.0f, bound);
    CHECK(f.interact);
    CHECK(!f.swing);
    const game_input space = game_input_from_keys(held(&input_state::space), 0.0f, bound);
    CHECK(space.swing);
    CHECK(space.jump);
    CHECK(!space.interact);
    // A left click does not address the ball or swing.
    const game_input click = game_input_from_keys(held(&input_state::mouse_left), 0.0f, bound);
    CHECK(!click.interact);
    CHECK(!click.swing);

    // Space beside the ball does not take it up; F does, and F again looks
    // down at it and stays there; only Esc steps away.
    game_state state = started_hole();
    const camera_tuning& camera = state.tuning.camera;
    update_game(state, game_input_from_keys(held(&input_state::space), 0.0f, bound), 0.016f);
    CHECK(state.mode == game_mode::walking);
    update_game(state, game_input_from_keys(held(&input_state::mouse_left), 0.0f, bound), 0.016f);
    CHECK(state.mode == game_mode::walking);
    update_game(state, game_input_from_keys(held(&input_state::key_f), 0.0f, bound), 0.016f);
    REQUIRE(state.mode == game_mode::addressing);
    REQUIRE(!looking_down(state.address.look, camera));
    for (int press = 0; press < 3; ++press) {
        update_game(state, game_input_from_keys(held(&input_state::key_f), 0.0f, bound), 0.016f);
        CHECK(state.mode == game_mode::addressing);
        for (int i = 0; i < 60; ++i) {
            update_game(state, game_input{}, 0.016f);
        }
        CHECK(looking_down(state.address.look, camera));
        CHECK(near(state.address.look.pitch, down_look(camera).pitch, 0.0001f));
    }
    update_game(state, game_input_from_keys(held(&input_state::escape), 0.0f, bound), 0.016f);
    CHECK(state.mode == game_mode::walking);
    update_game(state, game_input_from_keys(held(&input_state::key_f), 0.0f, bound), 0.016f);
    REQUIRE(state.mode == game_mode::addressing);

    // Looking down at it, Space takes the club back, Space again brings it
    // down, and the ball is struck at impact.
    state.address.look = down_look(camera);
    update_game(state, game_input_from_keys(held(&input_state::space), 0.0f, bound), 0.016f);
    REQUIRE(state.address.meter_elapsed.has_value());
    update_game(state, game_input{}, 0.5f);
    update_game(state, game_input_from_keys(held(&input_state::space), 0.0f, bound), 0.016f);
    REQUIRE(state.address.swing_elapsed.has_value());
    CHECK(state.ball->strokes == 0);
    swing_through(state);
    CHECK(state.ball->strokes == 1);
    CHECK(shot_playing(state));
}

TEST_CASE("looking down at the ball the arrows pull the face and the ball, W/S the backswing and A/D the stance; Backspace resets") {
    game_state state = started_hole();
    address_looking_down(state);
    const shot_setup stock = state.address.setup;
    const setup_tuning& tuning = state.tuning.setup;
    const auto press = [&](button_state input_state::*key) {
        update_game(state, game_input_from_keys(held(key), 0.0f, bound), 0.016f);
        update_game(state, game_input{}, 0.016f);
    };
    press(&input_state::up);
    CHECK(near(state.address.setup.face_open, stock.face_open + glm::radians(tuning.face_step_degrees), 0.0001f));
    press(&input_state::down);
    press(&input_state::down);
    CHECK(near(state.address.setup.face_open, stock.face_open - glm::radians(tuning.face_step_degrees), 0.0001f));
    // A right-hander's lead side is on the screen's left looking down: the ball goes forward.
    press(&input_state::left);
    CHECK(near(state.address.setup.ball_position, stock.ball_position + tuning.ball_position_step, 0.0001f));
    press(&input_state::right);
    press(&input_state::right);
    CHECK(near(state.address.setup.ball_position, stock.ball_position - tuning.ball_position_step, 0.0001f));
    press(&input_state::key_s);
    CHECK(state.address.setup.backswing < stock.backswing);
    press(&input_state::key_w);
    press(&input_state::key_w);
    CHECK(state.address.setup.backswing > stock.backswing);
    press(&input_state::key_a);
    CHECK(near(wrap_angle(state.address.setup.aim_angle - stock.aim_angle), glm::radians(tuning.aim_step_degrees), 0.0001f));
    press(&input_state::key_d);
    press(&input_state::key_d);
    CHECK(near(wrap_angle(state.address.setup.aim_angle - stock.aim_angle), -glm::radians(tuning.aim_step_degrees), 0.0001f));
    // None of them walks me away from the ball, and Backspace puts the stock setup back, keeping the aim.
    CHECK(state.mode == game_mode::addressing);
    const float aim = state.address.setup.aim_angle;
    press(&input_state::backspace);
    CHECK(state.address.setup.face_open == stock.face_open);
    CHECK(state.address.setup.ball_position == stock.ball_position);
    CHECK(state.address.setup.backswing == stock.backswing);
    CHECK(state.address.setup.aim_angle == aim);
    // My stance follows the setup.
    CHECK(near(state.player.position, address_stance_position(state.area, state.address.setup, state.tuning), 0.0001f));
}

TEST_CASE("looking up the arrows and a left-drag leave the face and ball be; A/D still turn the stance") {
    game_state state = started_hole();
    enter_addressing(state);
    REQUIRE(!looking_down(state.address.look, state.tuning.camera));
    const shot_setup stock = state.address.setup;
    for (button_state input_state::*key : {&input_state::up, &input_state::down, &input_state::left, &input_state::right}) {
        update_game(state, game_input_from_keys(held(key), 0.0f, bound), 0.016f);
        update_game(state, game_input{}, 0.016f);
    }
    game_input pull;
    pull.pull_x = 500.0f;
    pull.pull_y = 500.0f;
    update_game(state, pull, 0.016f);
    CHECK(state.address.setup.face_open == stock.face_open);
    CHECK(state.address.setup.ball_position == stock.ball_position);
    update_game(state, game_input_from_keys(held(&input_state::key_a), 0.0f, bound), 0.016f);
    CHECK(wrap_angle(state.address.setup.aim_angle - stock.aim_angle) > 0.0f);
}

TEST_CASE("looking down, the ball moves on the screen the way the arrow or the pull goes, for either hand") {
    for (const handedness hand : {handedness::right, handedness::left}) {
        game_state state = started_hole();
        state.hand = hand;
        address_looking_down(state);
        const setup_tuning& tuning = state.tuning.setup;
        // Where the ball sits across my view as I look down at it: + on the screen's right.
        const auto ball_on_screen = [&state]() -> float {
            const eye_view eye = my_address_eye(state);
            const glm::mat4 view = glm::lookAt(eye.position, eye.position + eye.direction, glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::vec4 clip = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.05f, 100.0f) * view *
                glm::vec4(state.address.setup.ball_start, 1.0f);
            CHECK(clip.w > 0.0f);
            return clip.x / clip.w;
        };
        const float start = ball_on_screen();
        update_game(state, game_input_from_keys(held(&input_state::left), 0.0f, bound), 0.016f);
        update_game(state, game_input{}, 0.016f);
        const float after_left = ball_on_screen();
        CHECK(after_left < start);
        // A left-drag right, a step's counts and more, moves it back right.
        game_input pull;
        pull.pull_x = static_cast<float>(tuning.pull_counts_per_step) * 2.5f;
        update_game(state, pull, 0.016f);
        CHECK(ball_on_screen() > after_left);
        // A pull up opens the face; down closes it.
        const float face = state.address.setup.face_open;
        pull = game_input{};
        pull.pull_y = -static_cast<float>(tuning.pull_counts_per_step);
        update_game(state, pull, 0.016f);
        CHECK(state.address.setup.face_open > face);
    }
}

TEST_CASE("a mouse wheel rolled away and a touchpad's fingers pushed up are both away from me: more backswing") {
    // A wheel's click away scrolls up.
    CHECK(wheel_steps_away(1, 0.0f, 1.0f, false) == 1);
    CHECK(wheel_steps_away(-2, 0.0f, -2.0f, false) == -2);
    // Fingers pushed up the touchpad scroll the page down, in fractions of a notch.
    CHECK(wheel_steps_away(-1, 0.0f, -1.25f, false) == 1);
    CHECK(wheel_steps_away(0, 0.1f, -0.4f, false) == 0);
    CHECK(wheel_steps_away(1, 0.0f, 0.75f, false) == -1);
    // A system that flips the wheel has it turned back first.
    CHECK(wheel_steps_away(-1, 0.0f, -1.0f, true) == 1);
    CHECK(wheel_steps_away(1, 0.0f, 1.25f, true) == 1);
}

TEST_CASE("a left-drag pulls instead of looking; the wheel sets the backswing; R and Backspace look and reset") {
    input_state keys;
    keys.mouse_left.is_down = true;
    keys.mouse_dx = 7;
    keys.mouse_dy = -3;
    const game_input pulling = game_input_from_keys(keys, 0.01f, bound);
    CHECK(pulling.pull_x == 7.0f);
    CHECK(pulling.pull_y == -3.0f);
    CHECK(pulling.look_yaw == 0.0f);
    CHECK(pulling.look_pitch == 0.0f);
    input_state wheel_up;
    wheel_up.wheel_steps = 1;
    CHECK(game_input_from_keys(wheel_up, 0.0f, bound).backswing_steps == 1);
    CHECK(game_input_from_keys(held(&input_state::key_r), 0.0f, bound).snap_view);
    CHECK(!game_input_from_keys(held(&input_state::key_r), 0.0f, bound).reset_setup);
    CHECK(game_input_from_keys(held(&input_state::backspace), 0.0f, bound).reset_setup);
    CHECK(game_input_from_keys(held(&input_state::mouse_middle), 0.0f, bound).reset_setup);
    CHECK(game_input_from_keys(held(&input_state::key_t), 0.0f, bound).aim_at_view);
    input_state right_click;
    right_click.mouse_right.released = true;
    CHECK(game_input_from_keys(right_click, 0.0f, bound).aim_at_view);

    // The wheel and W/S make the same steps, from either look.
    game_state by_key = started_hole();
    game_state by_wheel = started_hole();
    enter_addressing(by_key);
    enter_addressing(by_wheel);
    update_game(by_key, game_input_from_keys(held(&input_state::key_w), 0.0f, bound), 0.016f);
    update_game(by_wheel, game_input_from_keys(wheel_up, 0.0f, bound), 0.016f);
    CHECK(near(by_key.address.setup.backswing, by_wheel.address.setup.backswing, 0.0001f));
}

TEST_CASE("a right-drag past the threshold turns the stance (looking up) or swipes, and never also aims; a click aims") {
    input_state keys;
    keys.mouse_right.is_down = true;
    keys.mouse_dx = 4;
    keys.right_drag_counts = bound.drag_threshold_counts;
    const game_input small = game_input_from_keys(keys, 0.01f, bound);
    CHECK(small.stance_drag_yaw == 0.0f);
    CHECK(small.swipe == 0.0f);
    CHECK(near(small.look_yaw, -0.04f));
    keys.right_drag_counts = bound.drag_threshold_counts + 1;
    const game_input dragging = game_input_from_keys(keys, 0.01f, bound);
    CHECK(near(dragging.stance_drag_yaw, -0.04f));
    CHECK(near(dragging.swipe, 4.0f / static_cast<float>(bound.swipe_drag_counts)));
    CHECK(dragging.look_yaw == 0.0f);
    CHECK(dragging.look_pitch == 0.0f);
    CHECK(!dragging.aim_at_view);
    // Let go after a drag: no aim.
    keys.mouse_right.is_down = false;
    keys.mouse_right.released = true;
    keys.mouse_dx = 0;
    CHECK(!game_input_from_keys(keys, 0.01f, bound).aim_at_view);
    keys.right_drag_counts = 0;
    CHECK(game_input_from_keys(keys, 0.01f, bound).aim_at_view);
    // A touchpad's two fingers across swipe too: to the right they scroll
    // left, and swipe right.
    input_state fingers;
    fingers.wheel_x = -bound.swipe_wheel_amount;
    CHECK(near(game_input_from_keys(fingers, 0.0f, bound).swipe, 1.0f));
    CHECK(next_frame_input(fingers).wheel_x == 0.0f);

    // Looking up a drag turns the stance by its yaw, and the look not at all.
    game_state state = started_hole();
    enter_addressing(state);
    const shot_setup before = state.address.setup;
    const address_look look = state.address.look;
    game_input turn;
    turn.stance_drag_yaw = 0.02f;
    update_game(state, turn, 0.016f);
    CHECK(near(wrap_angle(state.address.setup.aim_angle - before.aim_angle), 0.02f, 0.0001f));
    CHECK(state.address.look.yaw == look.yaw);
    // Looking down it does not.
    state.address.look = down_look(state.tuning.camera);
    const float aim = state.address.setup.aim_angle;
    update_game(state, turn, 0.016f);
    CHECK(state.address.setup.aim_angle == aim);
}

TEST_CASE("right-click aims the path at the spot under the view and keeps the shape") {
    game_state state = started_hole();
    enter_addressing(state);
    state.address.setup.face_open = glm::radians(6.0f);
    const float shape = state.address.setup.face_open;
    // Look off to the side, along the ground some way out.
    state.address.look = clamped_look(address_look{glm::radians(60.0f), glm::radians(-8.0f)}, state.tuning.camera);
    REQUIRE(!looking_down(state.address.look, state.tuning.camera));
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

TEST_CASE("R turns my head between looking up and down over the snap time, with no frame where the view jumps") {
    game_state state = started_hole();
    enter_addressing(state);
    const camera_tuning& camera = state.tuning.camera;
    const address_look target = target_look(camera);
    const address_look down = down_look(camera);
    REQUIRE(near(state.address.look.yaw, target.yaw, 0.0001f));
    game_input r;
    r.snap_view = true;
    update_game(state, r, 0.016f);
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
    CHECK(looking_down(state.address.look, camera));
    // And back.
    update_game(state, r, dt);
    for (int i = 0; i < 60 && state.address.snap; ++i) {
        update_game(state, game_input{}, dt);
    }
    CHECK(near(state.address.look.yaw, target.yaw, 0.0001f));
    // R again mid-turn turns back the other way.
    update_game(state, r, dt);
    update_game(state, game_input{}, dt);
    update_game(state, r, dt);
    REQUIRE(state.address.snap.has_value());
    CHECK(near(state.address.snap->to.pitch, target.pitch, 0.0001f));
}

TEST_CASE("the swing starts only looking down at the ball") {
    for (const bool space : {false, true}) {
        game_state state = started_hole();
        enter_addressing(state);
        game_input start;
        start.swing = space;
        start.swipe = space ? 0.0f : 1.0f;
        update_game(state, start, 0.016f);
        CHECK(!state.address.meter_elapsed);
        state.address.look = down_look(state.tuning.camera);
        update_game(state, start, 0.016f);
        CHECK(state.address.meter_elapsed.has_value());
        CHECK(!state.address.swing_elapsed);
    }
}

TEST_CASE("swipe: a swipe right takes the club back slowly to the dialled top, a swipe left strikes from as far as it got") {
    const game_tuning tuning = started_hole().tuning;
    // Slower than the club goes back on its tempo.
    CHECK(swing_meter_at(tuning.swing.backswing_seconds, 1.0f, tuning.swing) < 1.0f);
    CHECK(swing_meter_at(100.0f, 0.7f, tuning.swing) == 0.7f);
    const float top = 0.7f * tuning.swing.meter_full_seconds;
    CHECK(strike_timing_at(top, 0.7f, tuning.swing) == 0.0f);
    CHECK(strike_timing_at(top - 0.5f * tuning.swing.timing_window_seconds, 0.7f, tuning.swing) == 0.0f);
    CHECK(strike_timing_at(top - tuning.swing.timing_window_seconds - 0.1f, 0.7f, tuning.swing) < 0.0f);
    CHECK(strike_timing_at(top + tuning.swing.timing_window_seconds + 0.1f, 0.7f, tuning.swing) > 0.0f);
    CHECK(strike_timing_at(0.0f, 0.7f, tuning.swing) == -1.0f);
    CHECK(strike_timing_at(100.0f, 0.7f, tuning.swing) == 1.0f);

    constexpr float dt = 1.0f / 60.0f;
    const auto swiped_left_after = [&](const float seconds) -> shot_setup {
        game_state state = started_hole();
        address_looking_down(state);
        state.address.setup.backswing = 0.7f;
        // Two half swipes right make a whole one.
        game_input half;
        half.swipe = 0.5f;
        update_game(state, half, dt);
        CHECK(!state.address.meter_elapsed);
        update_game(state, half, dt);
        CHECK(state.address.meter_elapsed.has_value());
        for (float elapsed = 0.0f; elapsed + dt <= seconds; elapsed += dt) {
            update_game(state, game_input{}, dt);
        }
        CHECK(!state.address.swing_elapsed);
        game_input left;
        left.swipe = -1.0f;
        update_game(state, left, dt);
        CHECK(state.address.swing_elapsed.has_value());
        CHECK(state.ball->strokes == 0);
        const shot_setup hit = state.address.setup;
        swing_through(state);
        CHECK(state.ball->strokes == 1);
        return hit;
    };
    const shot_setup early = swiped_left_after(0.3f * tuning.swing.meter_full_seconds);
    CHECK(early.backswing < 0.7f);
    CHECK(early.strike_timing < 0.0f);
    const shot_setup on_time = swiped_left_after(top);
    CHECK(near(on_time.backswing, 0.7f, 0.02f));
    CHECK(on_time.strike_timing == 0.0f);
    const shot_setup late = swiped_left_after(top + 1.0f);
    CHECK(late.backswing == 0.7f);
    CHECK(late.strike_timing > 0.0f);
}

TEST_CASE("swipe: Space goes back and Space comes down; Esc stops the backswing without leaving the ball") {
    game_state state = started_hole();
    address_looking_down(state);
    update_game(state, swing_input(), 0.016f);
    REQUIRE(state.address.meter_elapsed.has_value());
    game_input esc;
    esc.cancel = true;
    update_game(state, esc, 0.016f);
    CHECK(!state.address.meter_elapsed);
    CHECK(state.mode == game_mode::addressing);
    update_game(state, swing_input(), 0.016f);
    update_game(state, game_input{}, 0.5f);
    update_game(state, swing_input(), 0.016f);
    CHECK(state.address.swing_elapsed.has_value());
    swing_through(state);
    CHECK(state.ball->strokes == 1);
}

TEST_CASE("a mistimed strike goes shorter: early thin, late fat, on the same dice") {
    game_state state = started_hole();
    const club_definition& club = state.clubs[state.selected_club];
    shot_record record{stock_swing(club, state.tuning), 1234, {}};
    record.setup.ball_start = state.ball->position;
    const auto carry = [&](const float timing) {
        shot_record timed = record;
        timed.setup.strike_timing = timing;
        const shot_result result = simulate_struck_shot(timed, my_golfer(state), current_shot_course(state), state.tuning,
                                                        state.clubs);
        return horizontal_distance(result.rest_position, record.setup.ball_start);
    };
    const float on_time = carry(0.0f);
    CHECK(carry(-1.0f) < on_time);
    CHECK(carry(1.0f) < on_time);
    const shot_ability on_time_ability = ability_for(record.setup, club, ball_lie::tee, my_golfer(state), state.tuning);
    shot_setup thin = record.setup;
    thin.strike_timing = -1.0f;
    shot_setup fat = record.setup;
    fat.strike_timing = 1.0f;
    CHECK(ability_for(thin, club, ball_lie::tee, my_golfer(state), state.tuning).bias.strike_high < 0.0f);
    CHECK(ability_for(fat, club, ball_lie::tee, my_golfer(state), state.tuning).bias.speed_share < 0.0f);
    CHECK(ability_for(fat, club, ball_lie::tee, my_golfer(state), state.tuning).spread.strike_high >
          on_time_ability.spread.strike_high);
}

TEST_CASE("Space at the wheel does not jump or step out of the cart; F steps out") {
    game_state state = started_hole();
    sit_in_cart(state);
    update_game(state, game_input_from_keys(held(&input_state::space), 0.0f, bound), 0.016f);
    CHECK(driving_cart(state));
    CHECK(state.player.vertical.grounded);
    update_game(state, game_input_from_keys(held(&input_state::key_f), 0.0f, bound), 0.016f);
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
    const game_input input = game_input_from_keys(keys, 0.01f, bound);
    CHECK(near(input.look_yaw, -0.1f));
    CHECK(near(input.look_pitch, 0.04f));

    const game_input uncaptured = game_input_from_keys(keys, 0.0f, bound);
    CHECK(uncaptured.look_yaw == 0.0f);
    CHECK(uncaptured.look_pitch == 0.0f);
}

TEST_CASE("Shift runs, either Shift; Ctrl holds the rangefinder up") {
    CHECK(game_input_from_keys(held(&input_state::left_shift), 0.0f, bound).run_held);
    CHECK(game_input_from_keys(held(&input_state::right_shift), 0.0f, bound).run_held);
    CHECK(!game_input_from_keys(held(&input_state::left_shift), 0.0f, bound).rangefinder_held);
    CHECK(game_input_from_keys(held(&input_state::ctrl), 0.0f, bound).rangefinder_held);
    CHECK(!game_input_from_keys(held(&input_state::ctrl), 0.0f, bound).run_held);

    game_state state = started_hole();
    state.player.yaw = 0.3f;
    game_input run = game_input_from_keys(held(&input_state::left_shift), 0.0f, bound);
    run.forward_held = true;
    const float running = state.tuning.player.walk_speed * state.tuning.player.run_factor * 0.05f;
    CHECK(state.tuning.player.run_factor > 1.0f);
    CHECK(near(step_of(state, run, 0.05f), yaw_direction(0.3f) * running, 0.0001f));
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

TEST_CASE("over the ball the mouse looks freely within the clamp, never turning the stance") {
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
    down.look_pitch = -10.0f;
    update_game(state, down, 0.016f);
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

    // Walking moves in fixed steps: the hop has risen as far as the steps so far take it.
    const auto stepped = [](const float seconds) {
        return std::floor(seconds / move_step_seconds + 0.001f) * move_step_seconds;
    };
    game_input jump;
    jump.jump = true;
    jump.forward_held = true;
    update_game(state, jump, 0.02f);
    float elapsed = 0.02f;
    bool crossed = false;
    for (; !state.player.vertical.grounded && elapsed < 5.0f; elapsed += 0.02f) {
        CHECK(near(state.player.position.y, takeoff + rise(stepped(elapsed)), 0.0005f));
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
