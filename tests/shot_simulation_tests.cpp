#include "doctest.h"

#include "game/club_loader.h"
#include "game/content_files.h"
#include "game/course_session.h"
#include "game/game_state.h"
#include "game/json_util.h"
#include "game/reward_rules.h"
#include "game/shot_simulation.h"
#include "game/tuning_loader.h"
#include "physics/flight_model.h"
#include "physics/ground_contact.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
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

// No rolling deceleration on any surface but sand.
void no_roll_deceleration(game_state& state) {
    state.tuning.ball.green_roll_deceleration = 0.0f;
    state.tuning.ball.fairway_roll_deceleration = 0.0f;
    state.tuning.ball.rough_roll_deceleration = 0.0f;
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
    int lost = 0;
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
        lost += result.penalty_strokes;
    }
    // The shots cover the cup, the trees and the water, not only open ground.
    CHECK(holed > 0);
    CHECK(tree_hits > 0);
    CHECK(lost > 0);
}

TEST_CASE("a ball that goes under water sinks to the floor, lies there, and goes back to where it was hit") {
    game_state state = started_hole();
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 120.0f), 30.0f);
    hole.material_zones = {circle_zone(material_zone_type::water, glm::vec3(0.0f, 0.0f, 40.0f), 12.0f)};
    play_hole(state, hole);
    still_air(state);
    const shot_course course = current_shot_course(state);
    const terrain_sample pond = sample_area(state.area, glm::vec3(0.0f, 0.0f, 40.0f));
    REQUIRE(pond.material == terrain_material::water);
    REQUIRE(pond.water_level > pond.point.y + 1.0f);  // a bowl, deeper than a ball

    ball_state ball = state.ball;
    ball.position = glm::vec3(0.0f, pond.water_level + 3.0f, 40.0f);
    ball.velocity = glm::vec3(0.0f, -4.0f, 1.0f);
    const shot_result result = simulate_ball(ball, state.clubs[0].stats, course, state.tuning, 0.0f);

    CHECK(result.penalty_strokes == 1);
    CHECK(!result.holed);
    CHECK(result.rest_position == ball.position);  // the next shot is from where this one was
    // It ends on the floor (under the water), after lying there for the linger.
    const auto above_floor = [&](const glm::vec3& point) { return point.y - sample_area(state.area, point).point.y; };
    const glm::vec3 last = result.trajectory.back();
    CHECK(above_floor(last) < ball.radius + 0.01f);
    CHECK(last.y < pond.water_level);
    float reached_floor = result.duration;
    for (std::size_t i = 0; i < result.trajectory.size(); ++i) {
        if (above_floor(result.trajectory[i]) < ball.radius + 0.01f) {
            reached_floor = static_cast<float>(i) * shot_step_seconds * static_cast<float>(shot_steps_per_trajectory_point);
            break;
        }
    }
    CHECK(result.duration - reached_floor >= state.tuning.ball.water_linger_seconds - 0.05f);

    // A ball on dry ground is not lost.
    ball.position = glm::vec3(0.0f, 2.0f, 90.0f);
    CHECK(simulate_ball(ball, state.clubs[0].stats, course, state.tuning, 0.0f).penalty_strokes == 0);
}

TEST_CASE("from a bunker only a wedge gets the ball out properly") {
    game_state state = started_hole();
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 300.0f), 30.0f);
    hole.material_zones = {circle_zone(material_zone_type::bunker, glm::vec3(0.0f, 0.0f, 40.0f), 4.0f)};
    play_hole(state, hole);
    still_air(state);
    const auto club_named = [&](const std::string& id) {
        return *std::find_if(state.clubs.begin(), state.clubs.end(), [&](const club_definition& c) { return c.id == id; });
    };
    const auto carry = [&](const std::string& club, const glm::vec3& from) {
        shot_input input;
        input.ball_start = resting_on_terrain(state, from);
        input.aim_angle = yaw_towards(from, glm::vec3(0.0f, 0.0f, 300.0f));
        input.club_id = club;
        input.power = 1.0f;
        const shot_result result = simulate_shot(input, current_shot_course(state), state.tuning, state.clubs, state.rewards);
        return horizontal_distance(result.rest_position, input.ball_start);
    };
    REQUIRE(lie_at(state.area, glm::vec3(0.0f, 0.0f, 40.0f)) == ball_lie::bunker);
    const glm::vec3 sand(0.0f, 0.0f, 40.0f);  // off the tee box, which is a tee lie
    const glm::vec3 fairway(0.0f, 0.0f, 20.0f);
    REQUIRE(club_named("seven_iron").stats.bunker_power < club_named("sand_wedge").stats.bunker_power);
    // Sand cuts every club, and an iron loses more of its usual way than a sand wedge does.
    const float iron_kept = carry("seven_iron", sand) / carry("seven_iron", fairway);
    const float wedge_kept = carry("sand_wedge", sand) / carry("sand_wedge", fairway);
    CHECK(iron_kept < 1.0f);
    CHECK(wedge_kept < 1.0f);
    CHECK(iron_kept < wedge_kept);
}

TEST_CASE("a lie's power and spin scale the shot played from it") {
    game_state state = started_hole();
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 300.0f), 30.0f);
    hole.material_zones = {circle_zone(material_zone_type::bunker, glm::vec3(0.0f, 0.0f, 40.0f), 4.0f)};
    play_hole(state, hole);
    state.tuning.wind = wind_tuning{};  // drag and spin stay: spin must matter
    for (lie_tuning& lie : state.tuning.lies) {
        lie = lie_tuning{1.0f, 1.0f};
    }
    const auto carry = [&](const glm::vec3& from) {
        shot_input input;
        input.ball_start = resting_on_terrain(state, from);
        input.aim_angle = yaw_towards(from, glm::vec3(0.0f, 0.0f, 300.0f));
        input.club_id = "sand_wedge";
        input.power = 1.0f;
        const shot_result result = simulate_shot(input, current_shot_course(state), state.tuning, state.clubs, state.rewards);
        return horizontal_distance(result.rest_position, input.ball_start);
    };
    const glm::vec3 sand(0.0f, 0.0f, 40.0f);  // off the tee box, which is a tee lie
    const glm::vec3 fairway(0.0f, 0.0f, 20.0f);
    REQUIRE(lie_at(state.area, sand) == ball_lie::bunker);
    REQUIRE(lie_at(state.area, fairway) == ball_lie::fairway);
    const float from_fairway = carry(fairway);

    state.tuning.lies[static_cast<std::size_t>(ball_lie::bunker)].power = 0.5f;
    CHECK(carry(sand) < 0.7f * from_fairway);
    // Only the lie the ball is on counts.
    CHECK(carry(fairway) == from_fairway);
    state.tuning.lies[static_cast<std::size_t>(ball_lie::fairway)].spin = 0.0f;
    CHECK(carry(fairway) != from_fairway);
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

TEST_CASE("a rolling putt keeps more speed on the green than a rolling wedge shot") {
    game_state state = started_hole();
    still_air(state);
    state.tuning.ball.ground_restitution = 0.0f;
    state.tuning.ball.settle_speed = 10.0f;
    ball_state ball = resting_ball(state, state.hole->pin_position + glm::vec3(1.0f, 0.0f, 0.0f));
    REQUIRE(sample_area(state.area, ball.position).material == terrain_material::green);
    ball.velocity = glm::vec3(1.0f, 0.0f, 0.0f);

    const shot_step putt = step_once(state, ball, 0.05f, 0);
    const shot_step wedge = step_once(state, ball, 0.05f, 2);

    CHECK(state.clubs[0].stats.roll_friction_scale < 1.0f);
    CHECK(horizontal_speed(putt.ball.velocity) > horizontal_speed(wedge.ball.velocity));
}

TEST_CASE("a putt that rolls off the green is caught by the rough") {
    game_state state = started_hole();
    still_air(state);
    // The first rough beside the tee, away from the fairway.
    glm::vec3 start = state.hole->tee_position;
    while (sample_area(state.area, start).material != terrain_material::rough) {
        start.x += 0.5f;
    }
    start.x += 1.0f;
    REQUIRE(sample_area(state.area, start).material == terrain_material::rough);
    ball_state ball = resting_ball(state, start);
    ball.velocity = glm::vec3(3.0f, 0.0f, 0.0f);

    const shot_result putt = simulate_ball(ball, state.clubs[0].stats, current_shot_course(state), state.tuning, 0.0f);
    // v^2 / 2a: under a metre at the rough's deceleration, which the putter
    // does not soften off the green.
    CHECK(horizontal_distance(putt.rest_position, ball.position) < 1.5f);
}

TEST_CASE("contact friction does not depend on the step length") {
    game_state state = started_hole();
    still_air(state);
    state.tuning.ball.ground_restitution = 0.0f;
    no_roll_deceleration(state);
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
    state.tuning.ball.green_roll_deceleration = 10.0f;
    state.tuning.ball.fairway_roll_deceleration = 10.0f;
    state.tuning.ball.rough_roll_deceleration = 10.0f;
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
    no_roll_deceleration(state);
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
    no_roll_deceleration(state);
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
    hole.material_zones = {circle_zone(material_zone_type::water, glm::vec3(0.0f, 0.0f, 10.0f), 4.5f)};
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
        // On the pond's bed, under its water (on the grass, for the dry hole).
        ball.position = resting_on_terrain(*state, glm::vec3(0.0f, 0.0f, 10.0f));
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

namespace {
// A ball rolling along the green in +x, frictionless, from before the cup and
// `offset` to the side of its centre.
shot_result roll_past_cup(const float speed, const float offset) {
    game_state state = started_hole();
    still_air(state);
    state.tuning.ball.ground_restitution = 0.0f;
    state.tuning.ball.ground_friction = 0.0f;
    no_roll_deceleration(state);
    const shot_course course = current_shot_course(state);
    ball_state ball = state.ball;
    ball.position = course.hole.pin + glm::vec3(-(state.tuning.scale.cup_radius_meters + 0.25f), ball.radius, offset);
    ball.velocity = glm::vec3(speed, 0.0f, 0.0f);
    return simulate_ball(ball, state.clubs[0].stats, course, state.tuning, 0.0f);
}
}

TEST_CASE("a slow ball over the cup's centre drops in") {
    const game_state state = started_hole();
    const shot_result result = roll_past_cup(state.tuning.ball.cup_capture_speed * 0.5f, 0.0f);

    CHECK(result.holed);
    CHECK(result.rest_position.y < current_shot_course(state).hole.pin.y);
}

TEST_CASE("a fast ball over the cup's centre rolls over it") {
    const game_state state = started_hole();
    const shot_result result = roll_past_cup(state.tuning.ball.cup_capture_speed * 4.0f, 0.0f);

    CHECK(!result.holed);
}

TEST_CASE("a ball on the lip, just outside the cup, stays out") {
    const game_state state = started_hole();
    const shot_course course = current_shot_course(state);
    const float lip = state.tuning.scale.cup_radius_meters + 0.01f;

    const shot_result resting = simulate_ball(resting_ball(state, course.hole.pin + glm::vec3(lip, 0.0f, 0.0f)),
                                              state.clubs[0].stats, course, state.tuning, 0.0f);
    CHECK(!resting.holed);
    CHECK(horizontal_distance(resting.rest_position, course.hole.pin) > state.tuning.scale.cup_radius_meters);

    CHECK(!roll_past_cup(state.tuning.ball.cup_capture_speed * 0.5f, lip).holed);
}

TEST_CASE("a centred ball drops in faster than one crossing near the lip") {
    const game_state state = started_hole();
    const float speed = state.tuning.ball.cup_capture_speed * 0.9f;
    const float radius = state.tuning.scale.cup_radius_meters;

    CHECK(roll_past_cup(speed, 0.0f).holed);
    CHECK(!roll_past_cup(speed, radius * 0.95f).holed);
    // A real ball still drops at about 1.6 m/s over the centre; scaled to the
    // game's cup, a firm, well-centred putt does too.
    CHECK(roll_past_cup(2.5f, radius * 0.25f).holed);
}

namespace {
// A ball falling onto the green just short of the cup, so it comes down
// through the cup opening, with `forward` m/s horizontal and `down` m/s
// vertical speed.
shot_result fall_into_cup(const float forward, const float down) {
    game_state state = started_hole();
    still_air(state);
    const shot_course course = current_shot_course(state);
    const float height = 0.3f;
    const float fall_seconds = (std::sqrt(down * down + 2.0f * gravity_meters_per_second2 * height) - down) / gravity_meters_per_second2;
    ball_state ball = state.ball;
    ball.position = course.hole.pin + glm::vec3(-forward * fall_seconds, ball.radius + height, 0.0f);
    ball.velocity = glm::vec3(forward, -down, 0.0f);
    ball.spin = glm::vec3(0.0f);
    return simulate_ball(ball, state.clubs[0].stats, course, state.tuning, 0.0f);
}
}

TEST_CASE("a steep ball landing on the cup dunks, a fast low one skips over") {
    const game_state state = started_hole();
    // Faster than a rolling ball may drop, but falling steeply.
    const float forward = state.tuning.ball.cup_capture_speed * 1.6f;
    CHECK(fall_into_cup(forward, forward * 1.5f).holed);
    CHECK(!fall_into_cup(forward * 4.0f, 1.0f).holed);
}

namespace {
// A putt with the shipped putter and tuning, from `distance` short of `pin` on
// the line from the tee, aimed at the cup.
shot_result putt(const game_state& state, const glm::vec3& pin, const float distance, const float power) {
    shot_input input;
    input.ball_start = resting_on_terrain(state, pin - glm::vec3(0.0f, 0.0f, distance));
    input.aim_angle = yaw_towards(input.ball_start, pin);
    input.club_id = "putter";
    input.power = power;
    return simulate_shot(input, current_shot_course(state), state.tuning, state.clubs, state.rewards);
}
}

TEST_CASE("half power carries about half as far as full power with every club") {
    game_state state = make_game_state(shipped_content(), save_data{});
    // All green, so the putter rolls as it does on a green.
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 600.0f), 40.0f);
    hole.material_zones.push_back(material_zone{material_zone_type::green, glm::vec3(0.0f, 0.0f, 300.0f), glm::vec2(400.0f), 0.0f});
    play_hole(state, hole);
    state.tuning.wind = wind_tuning{};
    const auto carry = [&state](const std::string& club, const float power) {
        shot_input input;
        input.ball_start = resting_on_terrain(state, glm::vec3(0.0f));
        input.aim_angle = yaw_towards(input.ball_start, glm::vec3(0.0f, 0.0f, 600.0f));
        input.club_id = club;
        input.power = power;
        return horizontal_distance(simulate(state, input).rest_position, input.ball_start);
    };
    for (const club_definition& club : state.clubs) {
        const float full = carry(club.id, 1.0f);
        const float half = carry(club.id, 0.5f);
        CHECK(half > full * 0.38f);
        CHECK(half < full * 0.56f);
    }
}

TEST_CASE("a well-paced putt drops, a hard one runs past and a short one stops at the lip") {
    constexpr float power_step = 0.005f;
    // A flat green, one running downhill to the cup and one uphill.
    for (const float pin_height : {0.0f, -1.0f, 1.0f}) {
        game_state state = started_hole();
        const glm::vec3 pin(0.0f, pin_height, 20.0f);
        hole_data hole = straight_hole(glm::vec3(0.0f), pin, 20.0f);
        hole.material_zones.push_back(material_zone{material_zone_type::green, pin, glm::vec2(12.0f), 0.0f});
        play_hole(state, hole);
        const float radius = state.tuning.scale.cup_radius_meters;
        for (const float distance : {1.0f, 2.0f, 3.0f, 5.0f, 8.0f}) {
            // The softest putt that holes, and how many steps of the meter hole.
            float first = -1.0f;
            int holing = 0;
            for (float power = state.tuning.swing.min_power; power <= 1.0f; power += power_step) {
                if (putt(state, pin, distance, power).holed) {
                    first = first < 0.0f ? power : first;
                    ++holing;
                }
            }
            REQUIRE(first > 0.0f);
            // A short putt can be hit softer than it needs (downhill this
            // steep, the slope alone runs the softest putt down to the cup).
            if (pin_height >= 0.0f) {
                CHECK(first > state.tuning.swing.min_power);
            }
            // Not pixel-perfect: a decent swing has some room.
            CHECK(holing * power_step >= 0.04f);

            const float firm = first + static_cast<float>(holing) * power_step + 0.05f;
            const shot_result hard = putt(state, pin, distance, firm);
            CHECK(!hard.holed);
            CHECK(hard.rest_position.z > pin.z + radius);

            if (first - power_step >= state.tuning.swing.min_power) {
                const shot_result short_putt = putt(state, pin, distance, first - power_step);
                CHECK(!short_putt.holed);
                CHECK(short_putt.rest_position.z < pin.z);
                CHECK(horizontal_distance(short_putt.rest_position, pin) < radius + 0.3f);
            }
        }
    }
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
    CHECK(in_hub(state));
    CHECK(state.cup_ball.has_value());
    CHECK(state.stroke_count == 0);
    CHECK(state.mode == game_mode::walking);
    CHECK(state.save_requested);
}

TEST_CASE("holing the last hole finishes the round and completes the course") {
    game_state state = single_hole_course();
    state.stroke_count = 3;
    const shot_course course = current_shot_course(state);
    const glm::vec3 pin = course.hole.pin;
    play_shot(state, simulate_ball(resting_ball(state, pin), state.clubs[0].stats, course, state.tuning, 0.0f));

    play_out(state, 0.016f);

    CHECK(round_finished(state.round));
    CHECK(*state.round.strokes[0] == 3);
    CHECK(state.save.completed_course_ids == std::vector<std::string>{"fixture_hub"});
    CHECK(state.save.holes_completed == 1);
    CHECK(state.ball.position.y < pin.y);
}

namespace {
// A shipped course with its first bunker, and the fairway nearest that.
struct shipped_bunker {
    game_state state;
    glm::vec3 sand{0.0f};
    glm::vec3 fairway{0.0f};
};

std::optional<shipped_bunker> shipped_bunker_and_fairway() {
    shipped_bunker found{make_game_state(shipped_content(), save_data{})};
    const auto course = std::find_if(shipped_content().courses.begin(), shipped_content().courses.end(),
                                     [](const course_definition& c) { return c.id == "kalo_golf_club"; });
    if (course == shipped_content().courses.end() || !start_course(found.state, *course)) {
        return std::nullopt;
    }
    const auto zone = std::find_if(found.state.area.zones.begin(), found.state.area.zones.end(),
                                   [](const material_zone& z) { return z.type == material_zone_type::bunker; });
    if (zone == found.state.area.zones.end()) {
        return std::nullopt;
    }
    found.sand = resting_on_terrain(found.state, zone->center);
    float nearest = 1.0e9f;
    for (float x = -80.0f; x <= 80.0f; x += 2.0f) {
        for (float z = -80.0f; z <= 80.0f; z += 2.0f) {
            const glm::vec3 point = zone->center + glm::vec3(x, 0.0f, z);
            const float distance = horizontal_distance(point, zone->center);
            if (distance < nearest && lie_at(found.state.area, point) == ball_lie::fairway) {
                nearest = distance;
                found.fairway = resting_on_terrain(found.state, point);
            }
        }
    }
    if (nearest >= 1.0e9f) {
        return std::nullopt;
    }
    return found;
}
}

TEST_CASE("a ball in a shipped bunker lies in sand, and a driver from there goes nowhere") {
    const std::optional<shipped_bunker> found = shipped_bunker_and_fairway();
    REQUIRE(found.has_value());
    const shipped_bunker& course = *found;
    const game_state& state = course.state;
    REQUIRE(lie_at(state.area, course.sand) == ball_lie::bunker);
    REQUIRE(lie_at(state.area, course.fairway) == ball_lie::fairway);
    const auto longest_drive = [&](const glm::vec3& from) {
        float longest = 0.0f;
        for (int i = 0; i < 8; ++i) {  // every way round, so one tree cannot decide it
            shot_input input;
            input.ball_start = from;
            input.aim_angle = static_cast<float>(i) * glm::quarter_pi<float>();
            input.club_id = "driver";
            input.power = 1.0f;
            const shot_result result = simulate_shot(input, current_shot_course(state), state.tuning, state.clubs, state.rewards);
            longest = std::max(longest, horizontal_distance(result.rest_position, from));
        }
        return longest;
    };
    CHECK(longest_drive(course.sand) < 0.3f * longest_drive(course.fairway));
}

TEST_CASE("a ball landing in a shipped bunker plugs where it lands") {
    const std::optional<shipped_bunker> found = shipped_bunker_and_fairway();
    REQUIRE(found.has_value());
    const shipped_bunker& course = *found;
    const game_state& state = course.state;
    for (const float speed : {8.0f, 15.0f, 25.0f}) {
        ball_state ball;
        ball.radius = state.ball.radius;
        ball.position = course.sand + glm::vec3(-0.5f, 0.5f, 0.0f);
        ball.velocity = glm::vec3(speed, -speed, 0.0f);
        const shot_result result = simulate_ball(ball, state.clubs[0].stats, current_shot_course(state), state.tuning, 0.0f);
        REQUIRE(!result.events.empty());
        const glm::vec3 landed = shot_position_at(result, result.events.front().time);
        // No bounce: once down (past the trajectory point it lands between), it never rises off the sand.
        for (float time = result.events.front().time + 0.05f; time < result.duration; time += shot_step_seconds) {
            const glm::vec3 at = shot_position_at(result, time);
            CHECK(at.y - terrain_height(state.area, at) < ball.radius + 0.02f);
        }
        CHECK(lie_at(state.area, result.rest_position) == ball_lie::bunker);
        CHECK(horizontal_distance(result.rest_position, landed) < 0.6f);
    }
}
