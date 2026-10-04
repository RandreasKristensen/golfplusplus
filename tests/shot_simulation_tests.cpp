#include "doctest.h"

#include "game/club_loader.h"
#include "game/content_files.h"
#include "game/course_session.h"
#include "game/game_state.h"
#include "game/json_util.h"
#include "game/reward_rules.h"
#include "game/shot_simulation.h"
#include "game/tuning_loader.h"
#include "physics/ground_contact.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace {
// Fixture tuning, clubs and rewards frozen for the golden shots, so feel
// edits in assets/ do not move them. The shots are played on the fixture hub
// course, so an edit to its holes or course world moves them too. Replace the
// expected results in shots.json only when shots are meant to change; a
// mismatch prints the new ones.
std::string golden_root() {
    return fixture_root() + "/golden_shots";
}

game_content golden_content() {
    game_content content = fixture_content();
    content.tuning = *parse_game_tuning_from_text(*read_text_file(golden_root() + "/tuning/game_tuning.json")).tuning;
    content.rewards = *parse_rewards_from_text(*read_text_file(golden_root() + "/progression/rewards.json"));
    content.clubs = load_clubs_from_directory(golden_root() + "/clubs");
    return content;
}

// Hub hole `index` of the fixture hub course, ball on the tee.
game_state hub_hole(const game_content& content, const std::size_t index) {
    game_state state = make_game_state(content, save_data{});
    start_course(state, fixture_hub_course());
    start_hub_hole(state, index);
    return state;
}

shot_input tee_shot(const game_state& state, const std::string& club, const float power) {
    shot_input input;
    input.ball_start = state.ball.position;
    input.aim_angle = state.aim_angle;
    input.club_id = club;
    input.power = power;
    return input;
}

shot_result simulate(const game_state& state, const shot_input& input) {
    return simulate_shot(input, current_shot_course(state), state.tuning, state.clubs, state.rewards);
}

shot_step step_once(const game_state& state, const ball_state& ball, const float dt, const std::size_t club = 0) {
    return step_shot(ball, sample_area(state.area, ball.position), current_shot_course(state), state.clubs[club].stats,
                     state.tuning, 0.0f, dt);
}

// A ball sitting still on the ground at `position`.
ball_state resting_ball(const game_state& state, const glm::vec3& position) {
    ball_state ball = state.ball;
    ball.position = resting_on_terrain(state, position);
    ball.velocity = glm::vec3(0.0f);
    ball.spin = glm::vec3(0.0f);
    return ball;
}

bool same_result(const shot_result& a, const shot_result& b) {
    if (a.rest_position != b.rest_position || a.holed != b.holed || a.duration != b.duration ||
        a.trajectory != b.trajectory || a.events.size() != b.events.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.events.size(); ++i) {
        if (a.events[i].time != b.events[i].time || a.events[i].kind != b.events[i].kind ||
            a.events[i].material != b.events[i].material) {
            return false;
        }
    }
    return true;
}

// Updates until the playing shot has finished; returns the audio played.
std::vector<audio_event> play_out(game_state& state, const float dt) {
    std::vector<audio_event> played;
    for (int frame = 0; frame < 100000 && state.shot; ++frame) {
        update_game(state, game_input{}, dt);
        played.insert(played.end(), state.audio_events.begin(), state.audio_events.end());
        state.audio_events.clear();
    }
    return played;
}

int count_events(const shot_result& result, const shot_event_kind kind) {
    int count = 0;
    for (const shot_event& event : result.events) {
        count += event.kind == kind ? 1 : 0;
    }
    return count;
}

std::string event_names(const shot_result& result) {
    std::string names;
    for (const shot_event& event : result.events) {
        names += event.kind == shot_event_kind::land ? "L" : "T";
    }
    return names;
}

int count_audio(const std::vector<audio_event>& events, const audio_event_type type) {
    int count = 0;
    for (const audio_event& event : events) {
        count += event.type == type ? 1 : 0;
    }
    return count;
}
}

TEST_CASE("the same shot twice gives bit-identical results") {
    const game_state state = hub_hole(golden_content(), 0);
    shot_input input = tee_shot(state, "driver", 0.9f);
    input.wind_time = 4.0f;
    const shot_result first = simulate(state, input);

    // A second state, built separately and played in before the shot.
    game_state other = hub_hole(golden_content(), 0);
    hit_selected_club(other);
    play_out(other, 1.0f / 60.0f);
    const shot_result second = simulate(other, input);

    CHECK(first.duration > 0.0f);
    CHECK(first.trajectory.size() > 2U);
    CHECK(!first.events.empty());
    CHECK(same_result(first, second));
}

TEST_CASE("playback ends at the simulated rest whatever the frame rate") {
    const game_state start = hub_hole(golden_content(), 0);
    shot_input input = tee_shot(start, "driver", 0.8f);
    input.aim_angle += glm::radians(-45.0f);
    const shot_result result = simulate(start, input);
    REQUIRE(!result.holed);
    REQUIRE(count_events(result, shot_event_kind::tree_hit) > 0);

    std::vector<audio_event_type> expected;
    for (const shot_event& event : result.events) {
        expected.push_back(event.kind == shot_event_kind::land ? audio_event_type::ball_land : audio_event_type::ball_tree_hit);
    }
    for (const float dt : {1.0f / 30.0f, 1.0f / 144.0f}) {
        game_state state = start;
        play_shot(state, result);
        std::vector<audio_event_type> played;
        for (const audio_event& event : play_out(state, dt)) {
            played.push_back(event.type);
        }

        CHECK(played == expected);
        CHECK(!state.shot.has_value());
        CHECK(state.mode == game_mode::walking);
        CHECK(state.ball.position == result.rest_position);
        CHECK(state.flight_path_points.empty());
    }
}

TEST_CASE("golden shots land where they always have") {
    const game_content content = golden_content();
    const std::optional<std::string> text = read_text_file(golden_root() + "/shots.json");
    REQUIRE(text.has_value());
    const std::optional<json> root = parse_json(*text);
    REQUIRE(root.has_value());
    const json* shots = json_array(*root, "shots");
    REQUIRE(shots != nullptr);
    REQUIRE(!shots->empty());

    int holed = 0;
    int tree_hits = 0;
    for (std::size_t i = 0; i < shots->size(); ++i) {
        const json& shot = (*shots)[i];
        const game_state state = hub_hole(content, static_cast<std::size_t>(*json_int(shot, "hole")));
        shot_input input = tee_shot(state, *json_string(shot, "club"), *json_float(shot, "power"));
        // A putt from short of the pin, on the line from the tee.
        if (const std::optional<float> putt = json_float(shot, "putt_from_pin_meters")) {
            const glm::vec3 pin = current_shot_course(state).hole.pin;
            input.ball_start = resting_on_terrain(state, pin - yaw_direction(state.aim_angle) * *putt);
            input.aim_angle = yaw_towards(input.ball_start, pin);
        }
        input.aim_angle += glm::radians(*json_float(shot, "aim_offset_degrees"));
        input.wind_time = *json_float(shot, "wind_time");
        input.cigarette_active = *json_bool(shot, "cigarette");

        const shot_result result = simulate(state, input);
        const bool matches = near(result.rest_position, *json_vec3(shot, "rest"), 0.001f) &&
            result.holed == *json_bool(shot, "holed") && near(result.duration, *json_float(shot, "duration"), 0.0001f) &&
            event_names(result) == *json_string(shot, "events");
        if (!matches) {
            std::printf("golden shot %zu: \"rest\": [%.4f, %.4f, %.4f], \"holed\": %s, \"duration\": %.4f, \"events\": \"%s\"\n",
                        i, result.rest_position.x, result.rest_position.y, result.rest_position.z,
                        result.holed ? "true" : "false", result.duration, event_names(result).c_str());
        }
        CHECK(matches);
        holed += result.holed ? 1 : 0;
        tree_hits += count_events(result, shot_event_kind::tree_hit);
    }
    // The shots cover the cup and the trees, not only open ground.
    CHECK(holed > 0);
    CHECK(tree_hits > 0);
}

TEST_CASE("an unknown club leaves the ball where it is") {
    const game_state state = hub_hole(golden_content(), 0);
    const shot_result result = simulate(state, tee_shot(state, "no_such_club", 1.0f));

    CHECK(result.rest_position == state.ball.position);
    CHECK(result.duration == 0.0f);
    CHECK(!result.holed);
}

TEST_CASE("wind is sampled from the shot's wind time") {
    const game_state state = hub_hole(golden_content(), 0);
    REQUIRE(state.tuning.wind.base_speed + state.tuning.wind.speed_variation > 0.0f);
    shot_input calm = tee_shot(state, "driver", 1.0f);
    shot_input later = calm;
    later.wind_time = 37.0f;

    CHECK(simulate(state, calm).rest_position != simulate(state, later).rest_position);
}

TEST_CASE("a shot flies the same whichever way it is aimed") {
    const game_state state = started_hole();
    const club_stats& stats = state.clubs[4].stats;
    const ball_state north = launch_ball(glm::vec3(0.0f), 0.0f, stats, 0.7f, state.tuning);
    const ball_state east = launch_ball(glm::vec3(0.0f), glm::radians(90.0f), stats, 0.7f, state.tuning);

    // Rotate the east shot back onto north and compare.
    const glm::vec3 east_velocity_as_north(-east.velocity.z, east.velocity.y, east.velocity.x);
    const glm::vec3 east_spin_as_north(-east.spin.z, east.spin.y, east.spin.x);
    CHECK(near(east_velocity_as_north, north.velocity, 0.001f));
    CHECK(near(east_spin_as_north, north.spin, 0.001f));
}

TEST_CASE("swing power is clamped to the minimum and to full power") {
    game_state state = started_hole();
    state.tuning.swing.min_power = 0.4f;
    const club_stats& stats = state.clubs[0].stats;
    const auto speed = [&](const float power) {
        return glm::length(launch_ball(glm::vec3(0.0f), 0.0f, stats, power, state.tuning).velocity);
    };

    CHECK(speed(0.1f) == speed(0.4f));
    CHECK(speed(0.7f) > speed(0.4f));
    CHECK(speed(3.0f) == speed(1.0f));
    CHECK(speed(std::nanf("")) == speed(0.4f));
}

TEST_CASE("more loft launches the ball higher") {
    const game_state state = started_hole();
    const ball_state putt = launch_ball(glm::vec3(0.0f), 0.0f, state.clubs[0].stats, 1.0f, state.tuning);
    const ball_state wedge = launch_ball(glm::vec3(0.0f), 0.0f, state.clubs[1].stats, 1.0f, state.tuning);

    CHECK(state.clubs[1].stats.loft_degrees > state.clubs[0].stats.loft_degrees);
    CHECK(wedge.velocity.y / horizontal_speed(wedge.velocity) > putt.velocity.y / horizontal_speed(putt.velocity));
}

TEST_CASE("a cigarette changes the club only while it is active") {
    const game_state state = started_hole();
    const club_stats& club = state.clubs[1].stats;
    const club_stats smoking = shot_club_stats(club, true, state.rewards);

    CHECK(shot_club_stats(club, false, state.rewards).backspin == club.backspin);
    CHECK(smoking.backspin == club.backspin * state.rewards.cigarette.backspin_scale);
    CHECK(smoking.timing_speed == club.timing_speed * state.rewards.cigarette.timing_speed_scale);
}

TEST_CASE("a rolling putt keeps more speed than a rolling wedge shot") {
    game_state state = started_hole();
    still_air(state);
    state.tuning.ball.ground_restitution = 0.0f;
    state.tuning.ball.settle_speed = 10.0f;
    ball_state ball = resting_ball(state, state.hole->tee_position);
    ball.velocity = glm::vec3(1.0f, 0.0f, 0.0f);

    const shot_step putt = step_once(state, ball, 0.05f, 0);
    const shot_step wedge = step_once(state, ball, 0.05f, 2);

    CHECK(state.clubs[0].stats.roll_friction_scale < 1.0f);
    CHECK(horizontal_speed(putt.ball.velocity) > horizontal_speed(wedge.ball.velocity));
}

TEST_CASE("contact friction does not depend on the step length") {
    game_state state = started_hole();
    still_air(state);
    state.tuning.ball.ground_restitution = 0.0f;
    state.tuning.ball.roll_deceleration = 0.0f;
    state.tuning.ball.settle_speed = 10.0f;
    ball_state start = resting_ball(state, state.hole->tee_position);
    start.velocity = glm::vec3(3.0f, 0.0f, 0.0f);

    ball_state slow = start;
    for (int i = 0; i < 6; ++i) {
        slow = step_once(state, slow, 1.0f / 30.0f).ball;
    }
    ball_state fast = start;
    for (int i = 0; i < 12; ++i) {
        fast = step_once(state, fast, 1.0f / 60.0f).ball;
    }

    CHECK(near(horizontal_speed(slow.velocity), horizontal_speed(fast.velocity), 0.05f));
}

TEST_CASE("roll friction stops a grounded ball") {
    game_state state = started_hole();
    still_air(state);
    state.tuning.ball.ground_restitution = 0.0f;
    state.tuning.ball.roll_deceleration = 10.0f;
    state.tuning.ball.settle_speed = 10.0f;
    ball_state ball = resting_ball(state, state.hole->tee_position);
    ball.velocity = glm::vec3(1.0f, 0.0f, 0.0f);

    shot_step step{ball, sample_area(state.area, ball.position)};
    for (int i = 0; i < 8; ++i) {
        step = step_once(state, step.ball, 0.05f);
    }

    CHECK(horizontal_speed(step.ball.velocity) == 0.0f);
    CHECK(ball_at_rest(step.ball, step.ground, state.tuning.ball));
}

TEST_CASE("a ball on a slope keeps its velocity along the surface") {
    game_state state = started_hole();
    play_hole(state, straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 5.0f, 10.0f), 8.0f));
    state.tuning.ball.roll_deceleration = 0.0f;
    state.tuning.ball.settle_speed = 10.0f;

    const glm::vec3 tangent = glm::normalize(glm::vec3(0.0f, 5.0f, 10.0f));
    ball_state ball = resting_ball(state, glm::vec3(0.0f, 0.0f, 5.0f));
    ball.velocity = tangent;
    const shot_step step = step_once(state, ball, 0.0f);

    CHECK(step.ball.velocity.y > 0.1f);
    CHECK(glm::length(step.ball.velocity - tangent) < 0.001f);
}

TEST_CASE("the ball lands on the terrain height") {
    game_state state = started_hole();
    play_hole(state, straight_hole(glm::vec3(0.0f, 4.0f, 0.0f), glm::vec3(0.0f, 4.0f, 10.0f), 8.0f));
    still_air(state);
    state.tuning.ball.ground_restitution = 0.0f;
    state.tuning.ball.roll_deceleration = 0.0f;
    state.tuning.ball.settle_speed = 10.0f;
    ball_state ball = state.ball;
    ball.position = glm::vec3(0.0f, 4.05f, 5.0f);
    ball.velocity = glm::vec3(0.0f, -2.0f, 0.0f);

    const shot_step step = step_once(state, ball, 0.05f);

    const terrain_sample ground = sample_area(state.area, glm::vec3(0.0f, 0.0f, 5.0f));
    CHECK(near(glm::dot(step.ball.position - ground.point, ground.normal), ball.radius, 0.0001f));
    CHECK(step.ball.velocity.y == 0.0f);
    CHECK(step.landed);
}

TEST_CASE("the ball bounces off a tree trunk where the tree stands on the terrain") {
    game_state state = started_hole();
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 6.0f, 20.0f), 8.0f);
    hole.trees = {tree_instance{glm::vec3(12.0f, 0.0f, 10.0f), tree_shape{0.5f, 2.0f, 1.0f, 2.0f}}};
    play_hole(state, hole);
    still_air(state);
    state.tuning.ball.settle_speed = 10.0f;

    REQUIRE(state.static_anchors.trees.size() == 1U);
    const glm::vec3 base = state.static_anchors.trees.front().base;
    CHECK(near(base.x, 12.0f));
    CHECK(near(base.z, 10.0f));
    CHECK(base.y > 1.0f);

    ball_state ball = state.ball;
    ball.radius = 0.1f;
    ball.position = base + glm::vec3(0.35f, 0.8f, 0.0f);
    ball.velocity = glm::vec3(-3.0f, 0.0f, 0.0f);
    const shot_step step = step_once(state, ball, 0.016f);

    CHECK(step.ball.position.x >= 12.0f + 0.5f + 0.1f - 0.0001f);
    CHECK(step.ball.velocity.x > 0.0f);
    CHECK(step.hit_tree);
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

    std::vector<float> speeds;
    for (game_state* state : {&wet, &dry}) {
        still_air(*state);
        state->tuning.physics.drag_coeff = 0.01f;
        state->tuning.ball.ground_friction = 0.0f;
        state->tuning.ball.water_friction = 0.0f;
        ball_state ball = state->ball;
        ball.position = glm::vec3(0.0f, 0.0f, 10.0f);
        ball.velocity = glm::vec3(6.0f, 0.0f, 0.0f);
        speeds.push_back(horizontal_speed(step_once(*state, ball, 0.016f).ball.velocity));
    }

    CHECK(speeds[0] < speeds[1]);
}

TEST_CASE("a ball is at rest when it is slow and on the ground") {
    game_state state = started_hole();
    ball_state ball = resting_ball(state, state.hole->tee_position);
    ball.velocity = glm::vec3(1.0f, 0.0f, 0.0f);
    const terrain_sample ground = sample_area(state.area, ball.position);

    state.tuning.ball.stop_speed = 2.0f;
    CHECK(ball_at_rest(ball, ground, state.tuning.ball));
    state.tuning.ball.stop_speed = 0.5f;
    CHECK(!ball_at_rest(ball, ground, state.tuning.ball));

    state.tuning.ball.stop_speed = 2.0f;
    ball.position.y += 0.05f;
    CHECK(!ball_at_rest(ball, ground, state.tuning.ball));
}

TEST_CASE("a ball stopping within the cup's horizontal radius is holed") {
    const game_state state = started_hole();
    const shot_course course = current_shot_course(state);
    const club_stats& stats = state.clubs[0].stats;
    const float radius = state.tuning.scale.cup_radius_meters;

    const shot_result inside = simulate_ball(resting_ball(state, course.hole.pin + glm::vec3(radius - 0.01f, 0.0f, 0.0f)),
                                             stats, course, state.tuning, 0.0f);
    CHECK(inside.holed);
    CHECK(inside.rest_position.y < course.hole.pin.y);

    const shot_result outside = simulate_ball(resting_ball(state, course.hole.pin + glm::vec3(radius + 0.05f, 0.0f, 0.0f)),
                                              stats, course, state.tuning, 0.0f);
    CHECK(!outside.holed);
}

TEST_CASE("a fast ball crossing the cup drops in") {
    game_state state = started_hole();
    still_air(state);
    state.tuning.ball.ground_restitution = 0.0f;
    state.tuning.ball.ground_friction = 0.0f;
    state.tuning.ball.roll_deceleration = 0.0f;
    const shot_course course = current_shot_course(state);
    ball_state ball = state.ball;
    ball.position = course.hole.pin + glm::vec3(-(state.tuning.scale.cup_radius_meters + 0.25f), ball.radius, 0.0f);
    ball.velocity = glm::vec3(40.0f, 0.0f, 0.0f);

    const shot_result result = simulate_ball(ball, state.clubs[0].stats, course, state.tuning, 0.0f);

    CHECK(result.holed);
    CHECK(result.duration < 0.1f);
}

TEST_CASE("playback interpolates the trajectory and ends at the rest") {
    shot_result result;
    result.trajectory = {glm::vec3(0.0f), glm::vec3(2.0f, 0.0f, 0.0f), glm::vec3(3.0f, 0.0f, 0.0f)};
    result.duration = shot_steps_per_trajectory_point * shot_step_seconds * 1.5f;
    result.rest_position = glm::vec3(3.0f, 0.0f, 0.0f);
    const float interval = shot_steps_per_trajectory_point * shot_step_seconds;

    CHECK(shot_position_at(result, 0.0f) == glm::vec3(0.0f));
    CHECK(near(shot_position_at(result, interval * 0.5f), glm::vec3(1.0f, 0.0f, 0.0f)));
    CHECK(near(shot_position_at(result, interval * 1.25f), glm::vec3(2.5f, 0.0f, 0.0f)));
    CHECK(shot_position_at(result, result.duration) == result.rest_position);
    CHECK(shot_position_at(result, 99.0f) == result.rest_position);
}

TEST_CASE("a stopped shot outside the cup returns to walking without completing") {
    game_state state = started_hole();
    state.stroke_count = 2;
    const shot_course course = current_shot_course(state);
    const glm::vec3 beside = course.hole.pin + glm::vec3(state.tuning.scale.cup_radius_meters + 2.0f, 0.0f, 0.0f);
    play_shot(state, simulate_ball(resting_ball(state, beside), state.clubs[0].stats, course, state.tuning, 0.0f));

    play_out(state, 0.016f);

    CHECK(state.mode == game_mode::walking);
    CHECK(state.stroke_count == 2);
    CHECK(!hole_played(state.round, 0));
}

TEST_CASE("a holed shot completes the hole once it has played") {
    game_state state = started_hole();
    state.stroke_count = 2;
    const shot_course course = current_shot_course(state);
    play_shot(state, simulate_ball(resting_ball(state, course.hole.pin), state.clubs[0].stats, course, state.tuning, 0.0f));

    const std::vector<audio_event> played = play_out(state, 0.016f);

    CHECK(count_audio(played, audio_event_type::ball_cup) == 1);
    CHECK(hole_played(state.round, 0));
    CHECK(*state.round.strokes[0] == 2);
    CHECK(state.round.current_hole_index == 1);
    CHECK(state.hole->index == 1);
    CHECK(state.stroke_count == 0);
    CHECK(state.mode == game_mode::walking);
    CHECK(state.save_requested);
}

TEST_CASE("holing the last hole finishes the round and completes the course") {
    game_state state = started_game(fixture_course({"test"}));
    state.stroke_count = 3;
    const shot_course course = current_shot_course(state);
    const glm::vec3 pin = course.hole.pin;
    play_shot(state, simulate_ball(resting_ball(state, pin), state.clubs[0].stats, course, state.tuning, 0.0f));

    play_out(state, 0.016f);

    CHECK(round_finished(state.round));
    CHECK(*state.round.strokes[0] == 3);
    CHECK(state.save.completed_course_ids == std::vector<std::string>{"fixture_course"});
    CHECK(state.save.holes_completed == 1);
    CHECK(state.ball.position.y < pin.y);
}
