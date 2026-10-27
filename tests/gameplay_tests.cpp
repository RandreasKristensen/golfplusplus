#include "doctest.h"

#include "game/course_session.h"
#include "game/game_state.h"
#include "game/mode_dispatch.h"
#include "game/progression.h"
#include "game/swing.h"
#include "game/text_ids.h"

#include "test_support.h"

#include <cmath>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

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

TEST_CASE("at the wheel forward drives the cart, the player in its seat, and letting go slows it") {
    game_state state = started_hole();
    sit_in_cart(state);
    const glm::vec3 start = state.cart.pose.position;

    game_input input;
    input.forward_held = true;
    update_game(state, input, 0.05f);
    update_game(state, input, 0.05f);

    CHECK(state.mode == game_mode::walking);
    CHECK(driving_cart(state));
    CHECK(my_cart_speed(state) > 0.0f);
    CHECK(glm::length(state.cart.pose.position - start) > 0.0f);
    CHECK(near(horizontal(state.player.position), horizontal(seat_position(state.cart.pose, cart_seat::driver, state.tuning.cart)),
               0.0001f));

    const float moving = my_cart_speed(state);
    update_game(state, game_input{}, 0.05f);
    CHECK(my_cart_speed(state) < moving);
    CHECK(driving_cart(state));
}

TEST_CASE("interacting at the wheel steps out instead of starting a shot, the cart left where it stands") {
    game_state state = started_hole();
    sit_in_cart(state);
    const cart_pose parked = state.cart.pose;

    update_game(state, interact_input(), 0.016f);
    CHECK(state.ride.has_value());
    CHECK(!driving_cart(state));
    for (int i = 0; i < 60 && state.ride; ++i) {
        update_game(state, game_input{}, 0.05f);
    }

    CHECK(on_foot(state));
    CHECK(state.mode == game_mode::walking);
    CHECK(state.ball->strokes == 0);
    CHECK(near(state.cart.pose.position, parked.position));
    CHECK(near(horizontal(state.player.position),
               horizontal(step_out_position(parked, cart_seat::driver, state.tuning.cart)), 0.0001f));
}

TEST_CASE("leaving walking mode stands the player up, the cart left where it is") {
    game_state state = started_hole();
    sit_in_cart(state);
    state.cart.motion.velocity = glm::vec2(0.0f, 9.0f);
    const glm::vec3 parked = state.cart.pose.position;
    step_up_to_ball(state);

    update_game(state, game_input{}, 0.016f);

    CHECK(state.mode == game_mode::addressing);
    CHECK(on_foot(state));
    CHECK(my_cart_speed(state) == 0.0f);
    CHECK(near(state.cart.pose.position, parked));
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
    sit_in_cart(driver);
    game_input drive;
    drive.forward_held = true;
    for (int i = 0; i < 40; ++i) {
        update_game(driver, drive, 0.05f);
    }
    CHECK(driving_cart(driver));
    CHECK(xp_of(driver, driver.rewards.walking.skill_id) == 0);
}

TEST_CASE("what I smoke and what I drink play together, each for its use, and end on their own") {
    game_state state = started_hole();
    const item_definition& cigarette = *state.items.find(state.default_loadout.smokable);
    const item_definition& beer = *state.items.find(state.default_loadout.drinkable);
    REQUIRE(cigarette.use->xp_per_use.has_value());
    const xp_reward smoke_xp = *cigarette.use->xp_per_use;

    game_input smoke;
    smoke.use_smokable = true;
    update_game(state, smoke, 0.016f);
    REQUIRE(state.smoking.has_value());
    CHECK(state.smoking->item == cigarette.id);
    CHECK(!state.drinking.has_value());
    CHECK(xp_of(state, smoke_xp.skill_id) == smoke_xp.xp);
    REQUIRE(!state.audio_events.empty());
    CHECK(state.audio_events.back().type == audio_event_type::item_use);
    CHECK(state.audio_events.back().sound == cigarette.use->sound);

    game_input drink;
    drink.use_drinkable = true;
    update_game(state, drink, 0.016f);
    REQUIRE(state.drinking.has_value());
    CHECK(state.drinking->item == beer.id);
    CHECK(state.smoking.has_value());
    CHECK(state.smoking->elapsed > state.drinking->elapsed);

    // A use does not restart while it plays (the server refuses it too).
    update_game(state, smoke, 0.016f);
    CHECK(near(state.smoking->elapsed, 0.048f, 0.0001f));
    CHECK(xp_of(state, smoke_xp.skill_id) == smoke_xp.xp);

    const float longest = std::max(cigarette.use->seconds_per_use, beer.use->seconds_per_use);
    for (float waited = 0.0f; waited < longest + 0.1f; waited += 0.05f) {
        update_game(state, game_input{}, 0.05f);
    }
    CHECK(!state.smoking.has_value());
    CHECK(!state.drinking.has_value());

    update_game(state, smoke, 0.016f);
    CHECK(state.smoking.has_value());
    CHECK(xp_of(state, smoke_xp.skill_id) == 2 * smoke_xp.xp);
}

TEST_CASE("an item's timed effect raises XP while it runs, and walking speed by its bucket") {
    game_state state = started_hole();
    item_effect smoker;
    smoker.target = std::string(effect_target_xp_prefix) + state.rewards.shot.skill_id;
    smoker.bucket = effect_bucket::increased;
    smoker.value = 1.0f;
    smoker.duration = effect_duration::seconds;
    smoker.seconds = 30.0f;
    const std::int64_t now = shot_unix_ms(state);
    state.effects.push_back(active_effect{"lucky_charm", smoker, now, now + 30000});
    const int before = xp_of(state, state.rewards.shot.skill_id);
    record_shot(state, shot_setup{});
    CHECK(xp_of(state, state.rewards.shot.skill_id) - before == 2 * state.rewards.shot.xp);
    // Once it has run out, XP is as it was.
    state.local_unix_ms += 31000;
    record_shot(state, shot_setup{});
    CHECK(xp_of(state, state.rewards.shot.skill_id) - before == 3 * state.rewards.shot.xp);

    item_effect quicker;
    quicker.target = effect_target_walk_speed;
    quicker.bucket = effect_bucket::flat;
    quicker.value = 1.0f;
    quicker.duration = effect_duration::seconds;
    quicker.seconds = 30.0f;
    state.effects.push_back(active_effect{"shoes", quicker, shot_unix_ms(state), shot_unix_ms(state) + 30000});
    game_input forward;
    forward.forward_held = true;
    update_game(state, forward, 0.05f);
    CHECK(near(state.player.speed, state.tuning.player.walk_speed + 1.0f, 0.0001f));
}

TEST_CASE("interacting far from the ball does nothing") {
    game_state state = started_hole();
    state.player.position += glm::vec3(20.0f, 0.0f, 0.0f);

    update_game(state, interact_input(), 0.016f);

    CHECK(state.mode == game_mode::walking);
}

TEST_CASE("F near the ball steps up to it at the club's stock setup, looking at the target") {
    game_state state = started_hole();
    const float facing = state.player.yaw;

    enter_addressing(state);
    REQUIRE(state.mode == game_mode::addressing);
    const club_definition& club = state.clubs[state.selected_club];
    const shot_setup& setup = state.address.setup;
    CHECK(setup.club_id == club.id);
    CHECK(near(setup.aim_angle, wrap_angle(facing), 0.0001f));
    CHECK(setup.face_open == 0.0f);
    CHECK(setup.ball_position == club.spec.stock_ball_position);
    CHECK(setup.backswing == club.spec.stock_backswing);
    CHECK(setup.ball_start == state.ball->position);
    CHECK(near(state.address.look.yaw, target_look(state.tuning.camera).yaw, 0.0001f));
    CHECK(!state.address.swing_elapsed.has_value());
}

TEST_CASE("Space swings: the ball is struck at impact, on the club's tempo, not before") {
    for (const bool putter : {false, true}) {
        game_state state = started_hole();
        if (putter) {
            state.selected_club = state.clubs.size() - 1;
            REQUIRE(state.clubs[state.selected_club].club_class == "putter");
        }
        const club_definition& club = state.clubs[state.selected_club];
        const float impact = swing_impact_seconds(swing_tempo_for(club, state.tuning.swing));
        enter_addressing(state);
        update_game(state, swing_input(), 0.016f);
        REQUIRE(state.address.swing_elapsed.has_value());

        float elapsed = 0.0f;
        while (elapsed + 0.02f < impact) {
            update_game(state, game_input{}, 0.01f);
            elapsed += 0.01f;
            CHECK(state.mode == game_mode::addressing);
        }
        CHECK(state.ball->strokes == 0);
        CHECK(!state.shot.has_value());
        // Mid-swing nothing turns the setup or steps away.
        game_input meddle = interact_input();
        meddle.cancel = true;
        meddle.strafe_left_held = true;
        const shot_setup before = state.address.setup;
        update_game(state, meddle, 0.0f);
        CHECK(state.mode == game_mode::addressing);
        CHECK(state.address.setup.aim_angle == before.aim_angle);

        update_game(state, game_input{}, 0.05f);
        CHECK(state.mode == game_mode::following_shot);
        CHECK(state.ball->strokes == 1);
    }
    // A putt takes its own, slower tempo.
    const game_state state = started_hole();
    const swing_tempo full = swing_tempo_for(state.clubs.front(), state.tuning.swing);
    const swing_tempo putt = swing_tempo_for(state.clubs.back(), state.tuning.swing);
    CHECK(putt.back_seconds / putt.down_seconds < full.back_seconds / full.down_seconds);
}

TEST_CASE("addressing puts the player on the left of the ball") {
    game_state state = started_hole();
    state.player.yaw = 0.0f;

    enter_addressing(state);

    CHECK(state.mode == game_mode::addressing);
    CHECK(state.player.position.x > state.ball->position.x);
    CHECK(near(state.player.position.y, terrain_height(state.area, state.player.position)));
    // Out to the side by the stance's distance from the ball.
    const glm::vec3 side = yaw_left(yaw_direction(state.address.setup.aim_angle));
    CHECK(near(glm::dot(state.player.position - state.ball->position, side),
               state.tuning.lies.stance_ball_distance_meters / state.tuning.scale.meters_per_world_unit, 0.001f));

    // A left-hander stands on the other side.
    game_state lefty = started_hole();
    lefty.player.yaw = 0.0f;
    lefty.hand = handedness::left;
    enter_addressing(lefty);
    CHECK(lefty.address.setup.hand == handedness::left);
    CHECK(lefty.player.position.x < lefty.ball->position.x);
}

TEST_CASE("a held dial key steps once, then repeats faster the longer it is held") {
    game_state state = started_hole();
    enter_addressing(state);
    const float step = glm::radians(state.tuning.setup.aim_step_degrees);
    game_input hold;
    hold.strafe_left_held = true;
    const auto turned_over = [&](const float seconds) {
        const float before = state.address.setup.aim_angle;
        for (float t = 0.0f; t < seconds - 0.0001f; t += 0.01f) {
            update_game(state, hold, 0.01f);
        }
        return wrap_angle(state.address.setup.aim_angle - before);
    };
    // Pressed: one step, and nothing more until it repeats.
    CHECK(near(turned_over(0.01f), step, 0.0001f));
    CHECK(near(turned_over(state.tuning.setup.repeat_delay_seconds - 0.05f), 0.0f, 0.0001f));
    const float early = turned_over(0.3f);
    const float later = turned_over(0.3f);
    CHECK(early > 0.0f);
    CHECK(later > early);
    // Let go and pressed again: a single step again.
    update_game(state, game_input{}, 0.01f);
    CHECK(near(turned_over(0.01f), step, 0.0001f));
    // The arrows turn it as A and D do; D the other way.
    game_input right;
    right.turn_right_held = true;
    const float before = state.address.setup.aim_angle;
    update_game(state, game_input{}, 0.01f);
    update_game(state, right, 0.01f);
    CHECK(near(wrap_angle(state.address.setup.aim_angle - before), -step, 0.0001f));
}


TEST_CASE("cancel returns shot setup to walking") {
    game_state state = started_hole();
    enter_addressing(state);

    game_input cancel;
    cancel.cancel = true;
    update_game(state, cancel, 0.016f);

    CHECK(state.mode == game_mode::walking);
    CHECK(!state.address.swing_elapsed.has_value());
}

TEST_CASE("hitting launches the ball, counts a stroke and awards swing xp once") {
    game_state state = started_hole();
    hit_selected_club(state);

    CHECK(state.mode == game_mode::following_shot);
    CHECK(state.ball->strokes == 1);
    REQUIRE(state.shot.has_value());
    CHECK(state.shot->result.duration > 0.0f);
    CHECK(state.shot_start_position != glm::vec3(0.0f));
    CHECK(xp_of(state, state.rewards.shot.skill_id) == state.rewards.shot.xp);

    const glm::vec3 launched = state.ball->position;
    update_game(state, game_input{}, 0.016f);
    CHECK(glm::length(state.ball->position - launched) > 0.0f);
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
    CHECK(state.course_map_active);
    CHECK(state.scorecard_active);
    CHECK(state.skills_panel_active);

    update_game(state, game_input{}, 0.016f);
    CHECK(!state.rangefinder_active);
    CHECK(!state.course_map_active);
    CHECK(!state.scorecard_active);
    CHECK(!state.skills_panel_active);

    sit_in_cart(state);
    update_game(state, input, 0.016f);
    CHECK(!state.rangefinder_active);

    game_state addressing = started_hole();
    enter_addressing(addressing);
    update_game(addressing, input, 0.016f);
    CHECK(!addressing.rangefinder_active);
    CHECK(!addressing.course_map_active);
    CHECK(addressing.skills_panel_active);
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
    const glm::vec3 start = state.ball->position;
    const glm::vec3 pond = start + glm::vec3(0.0f, -1.0f, 30.0f);
    state.ball->strokes = 1;
    shot_result lost;
    lost.rest_position = start;
    lost.duration = 0.1f;
    lost.trajectory = {start, pond};
    lost.lost = true;
    lost.penalty_strokes = 1;
    play_shot(state, lost);
    for (int i = 0; i < 20 && state.shot; ++i) {
        update_game(state, game_input{}, 0.02f);
    }
    CHECK(!state.shot.has_value());
    CHECK(state.ball->strokes == 2);  // the shot was 1; the next one is played as 3
    CHECK(near(state.ball->position, start, 0.001f));
    REQUIRE(state.notice.has_value());
    CHECK(state.notice->text_key == text_hud_water_penalty);
}
