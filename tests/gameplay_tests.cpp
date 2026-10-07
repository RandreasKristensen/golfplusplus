#include "doctest.h"

#include "game/course_session.h"
#include "game/game_state.h"
#include "game/mode_dispatch.h"
#include "game/progression.h"
#include "game/text_ids.h"

#include "test_support.h"

#include <cmath>

#include <glm/geometric.hpp>

namespace {
int xp_of(const game_state& state, const std::string& skill) {
    return skill_xp(state.save.skills, skill);
}
}

TEST_CASE("walking forward moves the player and turning changes yaw") {
    game_state state = started_hole();
    const glm::vec3 start = state.player.position;
    const float start_yaw = state.player.yaw;

    game_input input;
    input.forward_held = true;
    input.turn_left_held = true;
    update_game(state, input, 0.05f);

    CHECK(glm::length(state.player.position - start) > 0.0f);
    CHECK(state.player.yaw > start_yaw);
    CHECK(state.mode == game_mode::walking);
}

TEST_CASE("walking follows the terrain height") {
    game_state state = started_hole();
    play_hole(state, straight_hole(glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(0.0f, 3.0f, 20.0f), 8.0f));
    state.player.position = glm::vec3(0.0f, 0.0f, 5.0f);
    state.player.yaw = 0.0f;

    game_input input;
    input.forward_held = true;
    update_game(state, input, 0.016f);

    CHECK(near(state.player.position.y, 3.0f));
}

TEST_CASE("holding the cart key drives the cart and releasing it parks") {
    game_state state = started_hole();
    const glm::vec3 start = state.player.position;

    game_input input;
    input.cart_held = true;
    update_game(state, input, 0.05f);
    update_game(state, input, 0.05f);

    CHECK(state.mode == game_mode::walking);
    CHECK(state.cart.active);
    CHECK(state.cart.velocity > 0.0f);
    CHECK(glm::length(state.player.position - start) > 0.0f);

    update_game(state, game_input{}, 0.016f);
    CHECK(!state.cart.active);
    CHECK(state.cart.velocity == 0.0f);
}

TEST_CASE("action in the cart drifts instead of starting a shot") {
    game_state state = started_hole();
    const float start_yaw = state.player.yaw;

    game_input input;
    input.cart_held = true;
    input.turn_left_held = true;
    input.action = true;
    update_game(state, input, 0.05f);

    CHECK(state.cart.active);
    CHECK(state.cart.drift_timer > 0.0f);
    CHECK(state.player.yaw > start_yaw);
    CHECK(state.mode == game_mode::walking);
    CHECK(state.stroke_count == 0);
}

TEST_CASE("leaving walking mode parks the cart") {
    game_state state = started_hole();
    state.mode = game_mode::aiming;
    state.cart = cart_state{true, 9.0f, 0.0f, 0.2f};

    update_game(state, game_input{}, 0.016f);

    CHECK(state.mode == game_mode::aiming);
    CHECK(!state.cart.active);
    CHECK(state.cart.velocity == 0.0f);
}

TEST_CASE("walking earns fitness xp but driving the cart does not") {
    game_state walker = started_hole();
    game_input walk;
    walk.forward_held = true;
    for (int i = 0; i < 40; ++i) {
        update_game(walker, walk, 0.05f);
    }
    CHECK(xp_of(walker, walker.rewards.walking.skill_id) > 0);

    game_state driver = started_hole();
    game_input drive;
    drive.cart_held = true;
    for (int i = 0; i < 40; ++i) {
        update_game(driver, drive, 0.05f);
    }
    CHECK(driver.cart.active);
    CHECK(xp_of(driver, driver.rewards.walking.skill_id) == 0);
}

TEST_CASE("smoke and drink emotes run together and end on their own") {
    game_state state = started_hole();
    const int smoke_xp = state.rewards.smoke.xp;

    game_input smoke;
    smoke.smoke = true;
    update_game(state, smoke, 0.016f);
    CHECK(state.smoke_emote.active);
    CHECK(!state.beer_emote.active);
    CHECK(state.cigarette_seconds_left > 0.0f);
    CHECK(xp_of(state, state.rewards.smoke.skill_id) == smoke_xp);

    game_input drink;
    drink.drink = true;
    update_game(state, drink, 0.016f);
    CHECK(state.smoke_emote.active);
    CHECK(state.beer_emote.active);
    CHECK(state.smoke_emote.elapsed > state.beer_emote.elapsed);

    // A smoke does not restart while one plays (the server refuses it too).
    update_game(state, smoke, 0.016f);
    CHECK(near(state.smoke_emote.elapsed, 0.048f, 0.0001f));
    CHECK(xp_of(state, state.rewards.smoke.skill_id) == smoke_xp);

    for (int i = 0; i < 60; ++i) {
        update_game(state, game_input{}, 0.05f);
    }
    CHECK(!state.smoke_emote.active);
    CHECK(!state.beer_emote.active);

    update_game(state, smoke, 0.016f);
    CHECK(state.smoke_emote.active);
    CHECK(xp_of(state, state.rewards.smoke.skill_id) == 2 * smoke_xp);
}

TEST_CASE("a cigarette slows the swing meter for a while") {
    game_state normal = started_hole();
    game_state smoking = started_hole();
    game_input smoke;
    smoke.smoke = true;
    update_game(smoking, smoke, 0.016f);

    for (game_state* state : {&normal, &smoking}) {
        enter_addressing(*state);
        update_game(*state, action_input(), 0.016f);
        update_game(*state, game_input{}, 0.05f);
    }

    CHECK(smoking.cigarette_seconds_left > 0.0f);
    CHECK(smoking.swing.power < normal.swing.power);
}

TEST_CASE("action far from the ball does nothing") {
    game_state state = started_hole();
    state.player.position += glm::vec3(20.0f, 0.0f, 0.0f);

    update_game(state, action_input(), 0.016f);

    CHECK(state.mode == game_mode::walking);
}

TEST_CASE("action near the ball aims, then addresses, then starts the meter") {
    game_state state = started_hole();

    enter_aiming(state);
    CHECK(state.mode == game_mode::aiming);
    CHECK(state.swing.phase == swing_phase::idle);

    update_game(state, action_input(), 0.016f);
    CHECK(state.mode == game_mode::addressing);
    CHECK(state.swing.phase == swing_phase::idle);

    update_game(state, action_input(), 0.016f);
    CHECK(state.swing.phase == swing_phase::timing);
    CHECK(state.stroke_count == 0);
    CHECK(!state.shot.has_value());
}

TEST_CASE("addressing puts the player on the left of the ball") {
    game_state state = started_hole();
    state.player.yaw = 0.0f;

    enter_addressing(state);

    CHECK(state.mode == game_mode::addressing);
    CHECK(state.player.position.x > state.ball.position.x);
    CHECK(near(state.player.position.y, terrain_height(state.area, state.player.position)));
}

TEST_CASE("aiming turns slower than walking") {
    game_state walking = started_hole();
    game_state aiming = started_hole();
    enter_aiming(aiming);

    game_input input;
    input.turn_left_held = true;
    const float walking_before = walking.player.yaw;
    const float aiming_before = aiming.aim_angle;
    update_game(walking, input, 0.05f);
    update_game(aiming, input, 0.05f);

    CHECK(std::abs(aiming.aim_angle - aiming_before) < std::abs(walking.player.yaw - walking_before));
}

TEST_CASE("club selection wraps in both directions") {
    game_state state = started_hole();
    enter_aiming(state);

    game_input shorter;
    shorter.shorter_club = true;
    update_game(state, shorter, 0.016f);
    CHECK(state.selected_club == state.clubs.size() - 1);

    game_input longer;
    longer.longer_club = true;
    update_game(state, longer, 0.016f);
    CHECK(state.selected_club == 0);
    update_game(state, longer, 0.016f);
    CHECK(state.selected_club == 1);
}

TEST_CASE("cancel returns shot setup to walking") {
    game_state state = started_hole();
    enter_addressing(state);

    game_input cancel;
    cancel.cancel = true;
    update_game(state, cancel, 0.016f);

    CHECK(state.mode == game_mode::walking);
    CHECK(state.swing.phase == swing_phase::idle);
}

TEST_CASE("hitting launches the ball, counts a stroke and awards swing xp once") {
    game_state state = started_hole();
    hit_selected_club(state);

    CHECK(state.mode == game_mode::following_shot);
    CHECK(state.stroke_count == 1);
    REQUIRE(state.shot.has_value());
    CHECK(state.shot->result.duration > 0.0f);
    CHECK(state.shot_start_position != glm::vec3(0.0f));
    CHECK(xp_of(state, state.rewards.shot.skill_id) == state.rewards.shot.xp);

    const glm::vec3 launched = state.ball.position;
    update_game(state, game_input{}, 0.016f);
    CHECK(glm::length(state.ball.position - launched) > 0.0f);
}

TEST_CASE("the putter meter runs at its own timing speed") {
    game_state putter = started_hole();
    game_state wedge = started_hole();
    wedge.selected_club = 1;
    for (game_state* state : {&putter, &wedge}) {
        enter_addressing(*state);
        update_game(*state, action_input(), 0.016f);
        update_game(*state, game_input{}, 0.05f);
    }

    CHECK(putter.clubs[0].stats.timing_speed < wedge.clubs[1].stats.timing_speed);
    CHECK(putter.swing.power < wedge.swing.power);
}

TEST_CASE("retee puts the ball back without a penalty stroke") {
    game_state state = started_hole();
    const glm::vec3 tee_ball = state.ball.position;
    hit_selected_club(state);
    for (int i = 0; i < 2000 && state.shot; ++i) {
        update_game(state, game_input{}, 0.05f);
    }
    REQUIRE(!state.shot.has_value());
    REQUIRE(state.hole->index == 0U);
    REQUIRE(state.ball.position != tee_ball);

    game_input retee;
    retee.retee = true;
    update_game(state, retee, 0.016f);

    CHECK(state.ball.position == tee_ball);
    CHECK(state.swing.phase == swing_phase::idle);
    CHECK(state.mode == game_mode::walking);
    CHECK(state.stroke_count == 1);
}

TEST_CASE("retee waits for a playing shot, which may hole") {
    game_state state = started_hole();
    hit_selected_club(state);
    update_game(state, game_input{}, 0.016f);
    REQUIRE(state.shot.has_value());
    const glm::vec3 in_flight = state.ball.position;

    game_input retee;
    retee.retee = true;
    update_game(state, retee, 0.016f);

    CHECK(state.shot.has_value());
    CHECK(state.ball.position != in_flight);  // still playing
}

TEST_CASE("overlays show only while held, and the rangefinder only on foot") {
    game_state state = started_hole();

    game_input input;
    input.rangefinder_held = true;
    input.course_map_held = true;
    input.scorecard_held = true;
    input.skills_panel_held = true;
    update_game(state, input, 0.016f);
    CHECK(state.rangefinder_active);
    CHECK(state.rangefinder_distance_meters > 0.0f);
    CHECK(state.course_map_active);
    CHECK(state.scorecard_active);
    CHECK(state.skills_panel_active);

    update_game(state, game_input{}, 0.016f);
    CHECK(!state.rangefinder_active);
    CHECK(!state.course_map_active);
    CHECK(!state.scorecard_active);
    CHECK(!state.skills_panel_active);

    input.cart_held = true;
    update_game(state, input, 0.016f);
    CHECK(!state.rangefinder_active);

    game_state aiming = started_hole();
    enter_aiming(aiming);
    update_game(aiming, input, 0.016f);
    CHECK(!aiming.rangefinder_active);
    CHECK(!aiming.course_map_active);
    CHECK(aiming.skills_panel_active);
}

TEST_CASE("the rangefinder distance shrinks walking towards the pin") {
    game_state state = started_hole();
    game_input input;
    input.rangefinder_held = true;
    update_game(state, input, 0.016f);
    const float before = state.rangefinder_distance_meters;

    input.forward_held = true;
    update_game(state, input, 0.05f);

    CHECK(state.rangefinder_distance_meters < before);
    CHECK(rounded_rangefinder_meters(124.49f) == 124);
    CHECK(rounded_rangefinder_meters(124.50f) == 125);
    CHECK(rounded_rangefinder_meters(-3.0f) == 0);
}

TEST_CASE("xp drops pool small gains and merge repeats of one skill") {
    game_state state = started_hole();
    const int threshold = state.tuning.xp_drops.min_visible_xp;
    REQUIRE(threshold > 1);
    state.play = play_mode::online;
    receive_online_progress(state, save_data{});
    const auto gain = [&state](const int xp) {
        save_data progress = state.online.progress;
        add_skill_xp(progress.skills, "fitness", xp);
        receive_online_progress(state, progress);
    };

    gain(1);
    CHECK(skill_xp(state.online.progress.skills, "fitness") == 1);
    CHECK(state.xp_drops.empty());

    gain(threshold - 1);
    REQUIRE(state.xp_drops.size() == 1U);
    CHECK(state.xp_drops[0].xp == threshold);

    update_xp_drops(state, 0.5f);
    gain(threshold);
    REQUIRE(state.xp_drops.size() == 1U);
    CHECK(state.xp_drops[0].xp == 2 * threshold);
    CHECK(state.xp_drops[0].age == 0.0f);

    update_xp_drops(state, state.tuning.xp_drops.lifetime_seconds - 0.01f);
    CHECK(state.xp_drops.size() == 1U);
    update_xp_drops(state, 0.02f);
    CHECK(state.xp_drops.empty());
}

TEST_CASE("a shot lost in water costs a stroke and is played again from where it was hit") {
    game_state state = started_hole();
    const glm::vec3 start = state.ball.position;
    const glm::vec3 pond = start + glm::vec3(0.0f, -1.0f, 30.0f);
    state.stroke_count = 1;
    shot_result lost;
    lost.rest_position = start;
    lost.duration = 0.1f;
    lost.trajectory = {start, pond};
    lost.penalty_strokes = 1;
    play_shot(state, lost);
    for (int i = 0; i < 20 && state.shot; ++i) {
        update_game(state, game_input{}, 0.02f);
    }
    CHECK(!state.shot.has_value());
    CHECK(state.stroke_count == 2);  // the shot was 1; the next one is played as 3
    CHECK(near(state.ball.position, start, 0.001f));
    REQUIRE(state.notice.has_value());
    CHECK(state.notice->text_key == text_hud_water_penalty);
}
