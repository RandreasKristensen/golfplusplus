#include "doctest.h"

#include "game/course_session.h"
#include "game/game_state.h"
#include "game/hole_loader.h"
#include "game/play_area.h"
#include "game/progression.h"
#include "physics/ground_contact.h"
#include "physics/terrain.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <cmath>

#include <glm/trigonometric.hpp>

#include <glm/geometric.hpp>

namespace {
// A straight hole from `tee` to `pin` on a ribbon of the given width.
hole_data straight_hole(const glm::vec3& tee, const glm::vec3& pin, const float width) {
    hole_data hole;
    hole.id = "straight";
    hole.name = "Straight";
    hole.par = 3;
    hole.tee_position = tee;
    hole.pin_position = pin;
    hole.spline.control_points = {tee, pin};
    hole.spline.width = width;
    hole.spline.rough_width = width;
    return hole;
}

// Replaces the hole being played with `hole`, as start_course would.
void play_hole(game_state& state, const hole_data& hole) {
    state.course_holes = {hole};
    state.round = start_round(1);
    state.area = build_hole_area(hole, state.tuning);
    state.hole = active_hole{0, hole.tee_position, hole.pin_position};
    mark_terrain_render_dirty(state);
}

// No wind, drag or spin, so ball motion depends only on contact.
void still_air(game_state& state) {
    state.tuning.wind = wind_tuning{};
    state.tuning.physics.drag_coeff = 0.0f;
    state.tuning.physics.magnus_coeff = 0.0f;
    state.tuning.physics.spin_decay = 0.0f;
}

float horizontal_speed(const glm::vec3& velocity) {
    return glm::length(horizontal(velocity));
}

glm::vec3 resting_on_terrain(const game_state& state, const glm::vec3& position) {
    const terrain_sample sample = sample_area(state.area, position);
    return sample.point + ground_normal(sample.normal) * state.ball.radius;
}

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

    update_game(state, smoke, 0.016f);
    CHECK(near(state.smoke_emote.elapsed, 0.016f));
    CHECK(xp_of(state, state.rewards.smoke.skill_id) == 2 * smoke_xp);

    for (int i = 0; i < 60; ++i) {
        update_game(state, game_input{}, 0.05f);
    }
    CHECK(!state.smoke_emote.active);
    CHECK(!state.beer_emote.active);
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
    CHECK(glm::length(state.ball.velocity) == 0.0f);
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

    game_input previous;
    previous.previous_club = true;
    update_game(state, previous, 0.016f);
    CHECK(state.selected_club == state.clubs.size() - 1);

    game_input next;
    next.next_club = true;
    update_game(state, next, 0.016f);
    CHECK(state.selected_club == 0);
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
    CHECK(glm::length(state.ball.velocity) > 0.0f);
    CHECK(state.shot_start_position != glm::vec3(0.0f));
    CHECK(xp_of(state, state.rewards.shot.skill_id) == state.rewards.shot.xp);

    const glm::vec3 launched = state.ball.position;
    update_game(state, game_input{}, 0.016f);
    CHECK(glm::length(state.ball.position - launched) > 0.0f);
}

TEST_CASE("a shot flies the same whichever way it is aimed") {
    game_state north = started_hole();
    game_state east = started_hole();
    north.player.yaw = 0.0f;
    east.player.yaw = glm::radians(90.0f);
    north.selected_club = 4;
    east.selected_club = 4;
    // Wind blows in a world direction, so it is turned off for the comparison.
    north.tuning.wind = wind_tuning{};
    east.tuning.wind = wind_tuning{};
    hit_selected_club(north);
    hit_selected_club(east);

    // Rotate the east shot back onto north and compare.
    const glm::vec3 east_velocity_as_north(-east.ball.velocity.z, east.ball.velocity.y, east.ball.velocity.x);
    const glm::vec3 east_spin_as_north(-east.ball.spin.z, east.ball.spin.y, east.ball.spin.x);
    CHECK(near(east_velocity_as_north, north.ball.velocity, 0.001f));
    CHECK(near(east_spin_as_north, north.ball.spin, 0.001f));
}

TEST_CASE("higher minimum power hits harder") {
    game_state soft = started_hole();
    game_state hard = started_hole();
    soft.tuning.swing.min_power = 0.1f;
    hard.tuning.swing.min_power = 0.8f;
    hit_selected_club(soft);
    hit_selected_club(hard);

    CHECK(glm::length(hard.ball.velocity) > glm::length(soft.ball.velocity));
}

TEST_CASE("more loft launches the ball higher") {
    game_state putter = started_hole();
    game_state wedge = started_hole();
    putter.tuning.swing.min_power = 1.0f;
    wedge.tuning.swing.min_power = 1.0f;
    wedge.selected_club = 1;
    hit_selected_club(putter);
    hit_selected_club(wedge);

    CHECK(wedge.clubs[1].stats.loft_degrees > putter.clubs[0].stats.loft_degrees);
    CHECK(wedge.ball.velocity.y / horizontal_speed(wedge.ball.velocity) >
          putter.ball.velocity.y / horizontal_speed(putter.ball.velocity));
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

TEST_CASE("a rolling putt keeps more speed than a rolling wedge shot") {
    game_state putt = started_hole();
    game_state wedge = started_hole();
    wedge.selected_club = 2;
    for (game_state* state : {&putt, &wedge}) {
        still_air(*state);
        state->tuning.ball.ground_restitution = 0.0f;
        state->tuning.ball.settle_speed = 10.0f;
        state->mode = game_mode::following_shot;
        state->ball.position = resting_on_terrain(*state, state->hole->tee_position);
        state->ball.velocity = glm::vec3(1.0f, 0.0f, 0.0f);
        update_game(*state, game_input{}, 0.05f);
    }

    CHECK(putt.clubs[0].stats.roll_friction_scale < 1.0f);
    CHECK(horizontal_speed(putt.ball.velocity) > horizontal_speed(wedge.ball.velocity));
}

TEST_CASE("contact friction does not depend on the frame rate") {
    game_state slow = started_hole();
    game_state fast = started_hole();
    for (game_state* state : {&slow, &fast}) {
        still_air(*state);
        state->tuning.ball.ground_restitution = 0.0f;
        state->tuning.ball.roll_deceleration = 0.0f;
        state->tuning.ball.settle_speed = 10.0f;
        state->mode = game_mode::following_shot;
        state->ball.position = resting_on_terrain(*state, state->hole->tee_position);
        state->ball.velocity = glm::vec3(3.0f, 0.0f, 0.0f);
    }
    for (int i = 0; i < 6; ++i) {
        update_game(slow, game_input{}, 1.0f / 30.0f);
    }
    for (int i = 0; i < 12; ++i) {
        update_game(fast, game_input{}, 1.0f / 60.0f);
    }

    CHECK(near(horizontal_speed(slow.ball.velocity), horizontal_speed(fast.ball.velocity), 0.05f));
}

TEST_CASE("roll friction stops a grounded ball") {
    game_state state = started_hole();
    still_air(state);
    state.tuning.ball.ground_restitution = 0.0f;
    state.tuning.ball.roll_deceleration = 10.0f;
    state.tuning.ball.settle_speed = 10.0f;
    state.mode = game_mode::following_shot;
    state.ball.position = resting_on_terrain(state, state.hole->tee_position);
    state.ball.velocity = glm::vec3(1.0f, 0.0f, 0.0f);

    for (int i = 0; i < 8; ++i) {
        update_game(state, game_input{}, 0.05f);
    }

    CHECK(horizontal_speed(state.ball.velocity) == 0.0f);
    CHECK(!ball_is_moving(state));
    CHECK(state.mode == game_mode::walking);
}

TEST_CASE("a ball on a slope keeps its velocity along the surface") {
    game_state state = started_hole();
    play_hole(state, straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 5.0f, 10.0f), 8.0f));
    state.tuning.ball.roll_deceleration = 0.0f;
    state.tuning.ball.settle_speed = 10.0f;
    state.tuning.ball.stop_speed = 0.01f;
    state.mode = game_mode::following_shot;

    const glm::vec3 tangent = glm::normalize(glm::vec3(0.0f, 5.0f, 10.0f));
    state.ball.position = resting_on_terrain(state, glm::vec3(0.0f, 0.0f, 5.0f));
    state.ball.velocity = tangent;
    update_game(state, game_input{}, 0.0f);

    CHECK(state.ball.velocity.y > 0.1f);
    CHECK(glm::length(state.ball.velocity - tangent) < 0.001f);
}

TEST_CASE("the ball lands on the terrain height") {
    game_state state = started_hole();
    play_hole(state, straight_hole(glm::vec3(0.0f, 4.0f, 0.0f), glm::vec3(0.0f, 4.0f, 10.0f), 8.0f));
    still_air(state);
    state.tuning.ball.ground_restitution = 0.0f;
    state.tuning.ball.roll_deceleration = 0.0f;
    state.tuning.ball.settle_speed = 10.0f;
    state.mode = game_mode::following_shot;
    state.ball.position = glm::vec3(0.0f, 4.05f, 5.0f);
    state.ball.velocity = glm::vec3(0.0f, -2.0f, 0.0f);

    update_game(state, game_input{}, 0.05f);

    const terrain_sample ground = sample_area(state.area, glm::vec3(0.0f, 0.0f, 5.0f));
    CHECK(near(glm::dot(state.ball.position - ground.point, ground.normal), state.ball.radius, 0.0001f));
    CHECK(state.ball.velocity.y == 0.0f);
}

TEST_CASE("the ball bounces off a tree trunk where the tree stands on the terrain") {
    game_state state = started_hole();
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 6.0f, 20.0f), 8.0f);
    hole.trees = {tree_instance{glm::vec3(12.0f, 0.0f, 10.0f), tree_shape{0.5f, 2.0f, 1.0f, 2.0f}}};
    play_hole(state, hole);
    still_air(state);
    state.tuning.ball.stop_speed = 0.01f;
    state.tuning.ball.settle_speed = 10.0f;
    state.mode = game_mode::following_shot;

    refresh_static_anchor_cache(state);
    REQUIRE(state.static_anchors.trees.size() == 1U);
    const glm::vec3 base = state.static_anchors.trees.front().base;
    CHECK(near(base.x, 12.0f));
    CHECK(near(base.z, 10.0f));
    CHECK(base.y > 1.0f);

    state.ball.radius = 0.1f;
    state.ball.position = base + glm::vec3(0.35f, 0.8f, 0.0f);
    state.ball.velocity = glm::vec3(-3.0f, 0.0f, 0.0f);
    update_game(state, game_input{}, 0.016f);

    CHECK(state.ball.position.x >= 12.0f + 0.5f + 0.1f - 0.0001f);
    CHECK(state.ball.velocity.x > 0.0f);
}

TEST_CASE("water slows a ball more than grass") {
    game_state wet = started_hole();
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 20.0f), 10.0f);
    material_zone water;
    water.type = material_zone_type::water;
    water.center = glm::vec3(0.0f, 0.0f, 10.0f);
    water.radius = 4.5f;
    water.has_radius = true;
    hole.material_zones = {water};
    play_hole(wet, hole);

    game_state dry = started_hole();
    play_hole(dry, straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 20.0f), 10.0f));

    for (game_state* state : {&wet, &dry}) {
        still_air(*state);
        state->tuning.physics.drag_coeff = 0.01f;
        state->tuning.ball.ground_friction = 0.0f;
        state->tuning.ball.water_friction = 0.0f;
        state->mode = game_mode::following_shot;
        state->ball.position = glm::vec3(0.0f, 0.0f, 10.0f);
        state->ball.velocity = glm::vec3(6.0f, 0.0f, 0.0f);
        update_game(*state, game_input{}, 0.016f);
    }

    CHECK(horizontal_speed(wet.ball.velocity) < horizontal_speed(dry.ball.velocity));
}

TEST_CASE("the moving threshold follows stop speed and terrain contact") {
    game_state state = started_hole();
    state.ball.position = resting_on_terrain(state, state.hole->tee_position);
    state.ball.velocity = glm::vec3(1.0f, 0.0f, 0.0f);

    state.tuning.ball.stop_speed = 2.0f;
    CHECK(!ball_is_moving(state));
    state.tuning.ball.stop_speed = 0.5f;
    CHECK(ball_is_moving(state));

    state.ball.velocity = glm::vec3(0.0f);
    state.ball.position.y += 0.05f;
    CHECK(ball_is_moving(state));
}

TEST_CASE("a stopped ball outside the cup returns to walking without completing") {
    game_state state = started_hole();
    state.stroke_count = 2;
    state.mode = game_mode::following_shot;
    const glm::vec3 pin = pin_anchor_position(state);
    state.ball.position = resting_on_terrain(state, pin + glm::vec3(state.tuning.scale.cup_radius_meters + 2.0f, 0.0f, 0.0f));

    update_game(state, game_input{}, 0.016f);

    CHECK(state.mode == game_mode::walking);
    CHECK(state.stroke_count == 2);
    CHECK(!hole_played(state.round, 0));
}

TEST_CASE("the cup is a horizontal radius around the terrain-anchored pin") {
    game_state state = started_hole();
    const glm::vec3 pin = pin_anchor_position(state);
    const float radius = state.tuning.scale.cup_radius_meters;

    state.ball.position = pin;
    CHECK(ball_is_in_cup(state));
    state.ball.position = pin + glm::vec3(radius - 0.01f, 12.0f, 0.0f);
    CHECK(ball_is_in_cup(state));
    state.ball.position = pin + glm::vec3(radius + 0.01f, 12.0f, 0.0f);
    CHECK(!ball_is_in_cup(state));
}

TEST_CASE("a ball stopping in the cup completes the hole") {
    game_state state = started_hole();
    state.stroke_count = 2;
    state.mode = game_mode::following_shot;
    state.ball.position = pin_anchor_position(state);

    update_game(state, game_input{}, 0.016f);

    CHECK(hole_played(state.round, 0));
    CHECK(*state.round.strokes[0] == 2);
    CHECK(state.round.current_hole_index == 1);
    CHECK(state.hole->index == 1);
    CHECK(state.stroke_count == 0);
    CHECK(state.mode == game_mode::walking);
    CHECK(state.save_requested);
}

TEST_CASE("a fast ball crossing the cup drops in") {
    game_state state = started_hole();
    still_air(state);
    state.tuning.ball.ground_restitution = 0.0f;
    state.tuning.ball.ground_friction = 0.0f;
    state.tuning.ball.roll_deceleration = 0.0f;
    const glm::vec3 pin = pin_anchor_position(state);
    state.stroke_count = 1;
    state.mode = game_mode::following_shot;
    state.ball.position = pin + glm::vec3(-(state.tuning.scale.cup_radius_meters + 0.25f), state.ball.radius, 0.0f);
    state.ball.velocity = glm::vec3(40.0f, 0.0f, 0.0f);

    update_game(state, game_input{}, 0.016f);

    CHECK(hole_played(state.round, 0));
    CHECK(*state.round.strokes[0] == 1);
}

TEST_CASE("holing the last hole finishes the round and completes the course") {
    game_state state = started_game(fixture_course({"test"}));
    state.stroke_count = 3;
    const glm::vec3 pin = pin_anchor_position(state);
    state.ball.position = pin;

    update_game(state, game_input{}, 0.016f);

    CHECK(round_finished(state.round));
    CHECK(*state.round.strokes[0] == 3);
    CHECK(state.save.completed_course_ids == std::vector<std::string>{"fixture_course"});
    CHECK(state.save.holes_completed == 1);
    CHECK(state.ball.position.y < pin.y);
}

TEST_CASE("retee puts the ball back without a penalty stroke") {
    game_state state = started_hole();
    const glm::vec3 tee_ball = state.ball.position;
    enter_addressing(state);
    update_game(state, action_input(), 0.016f);
    state.ball.position = glm::vec3(5.0f, 1.0f, 6.0f);
    state.ball.velocity = glm::vec3(2.0f, 3.0f, 4.0f);
    state.stroke_count = 1;

    game_input retee;
    retee.retee = true;
    update_game(state, retee, 0.016f);

    CHECK(state.ball.position == tee_ball);
    CHECK(glm::length(state.ball.velocity) == 0.0f);
    CHECK(state.swing.phase == swing_phase::idle);
    CHECK(state.mode == game_mode::walking);
    CHECK(state.stroke_count == 1);
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

    award_skill_xp(state, xp_reward{"fitness", 1});
    CHECK(xp_of(state, "fitness") == 1);
    CHECK(state.xp_drops.empty());

    award_skill_xp(state, xp_reward{"fitness", threshold - 1});
    REQUIRE(state.xp_drops.size() == 1U);
    CHECK(state.xp_drops[0].xp == threshold);

    update_xp_drops(state, 0.5f);
    award_skill_xp(state, xp_reward{"fitness", threshold});
    REQUIRE(state.xp_drops.size() == 1U);
    CHECK(state.xp_drops[0].xp == 2 * threshold);
    CHECK(state.xp_drops[0].age == 0.0f);

    update_xp_drops(state, state.tuning.xp_drops.lifetime_seconds - 0.01f);
    CHECK(state.xp_drops.size() == 1U);
    update_xp_drops(state, 0.02f);
    CHECK(state.xp_drops.empty());
}
