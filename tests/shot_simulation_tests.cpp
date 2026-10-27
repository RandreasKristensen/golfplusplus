#include "doctest.h"

#include "game/mode_dispatch.h"
#include "physics/turf_contact.h"
#include "game/area_solids.h"
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
// Fixture tuning, items (the clubs among them) and rewards frozen for the golden shots, so feel
// edits in assets/ do not move them. The shots are played on the fixture
// course, so an edit to its holes or course world moves them too. Replace the
// expected results in shots.json only when shots are meant to change; a
// mismatch prints the new ones.
std::string golden_root() {
    return fixture_root() + "/golden_shots";
}

game_content golden_content() {
    game_content content = fixture_content();
    content.tuning = *parse_game_tuning_from_texts(json_texts_in_directory(golden_root() + "/play/tuning")).tuning;
    content.rewards = *parse_rewards_from_text(*read_text_file(golden_root() + "/play/progression/rewards.json"));
    content.items = *load_items(golden_root()).catalog;
    content.clubs = clubs_of(content.items);
    return content;
}

// Hole `index` of the fixture course, my ball on its tee.
game_state teed_hole(const game_content& content, const std::size_t index) {
    game_state state = make_game_state(content, save_data{});
    start_course(state, fixture_hub_course());
    tee_up(state, index, false);
    return state;
}

shot_setup tee_shot(const game_state& state, const std::string& club, const float power) {
    shot_setup input;
    input.ball_start = state.ball->position;
    input.aim_angle = state.player.yaw;
    input.club_id = club;
    input = stocked(input, state, power);
    return input;
}

// In the course's weather at the shot's time, as launch_shot plays it.
shot_result simulate(const game_state& state, const shot_setup& input) {
    shot_course course = current_shot_course(state);
    course.weather = course_weather_at(state, input.shot_at);
    return simulate_shot(input, course, state.tuning, state.clubs);
}

// The bag's last club.
const club_definition& putter_of(const game_state& state) {
    return state.clubs.back();
}

const club_definition& club_named(const game_state& state, const std::string& id) {
    return *std::find_if(state.clubs.begin(), state.clubs.end(), [&id](const club_definition& c) { return c.id == id; });
}

shot_step step_once(const game_state& state, const ball_state& ball, const float dt) {
    return step_shot(ball, sample_area(state.area, ball.position), current_shot_course(state), state.tuning, dt);
}

// A ball sitting still on the ground at `position`.
ball_state resting_ball(const game_state& state, const glm::vec3& position) {
    ball_state ball = still_ball(state);
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
        // Land, Tree, Pole, Board, Flag, Cup, leaVes, Human, Other ball.
        names += "LTPBFCVHO"[static_cast<int>(event.kind)];
    }
    return names;
}

// A golden shot's snapshot: each entry `ahead_meters` along the aim from
// the ball, on the ground (a ball resting there).
std::vector<snapshot_entry> golden_snapshot(const json& shot, const play_area& area, const glm::vec3& ball, const float aim,
                                            const float ball_radius) {
    std::vector<snapshot_entry> entries;
    if (const json* listed = json_array(shot, "snapshot")) {
        for (const json& entry : *listed) {
            const std::string kind = json_string(entry, "kind").value_or("person");
            const glm::vec3 at = ball + yaw_direction(aim) * json_float(entry, "ahead_meters").value_or(0.0f);
            const snapshot_kind of = kind == "cart" ? snapshot_kind::cart : kind == "ball" ? snapshot_kind::ball : snapshot_kind::person;
            const glm::vec3 placed = of == snapshot_kind::ball ? resting_ball_position(area, at, ball_radius)
                                                               : anchor_on_terrain(area, at);
            entries.push_back(snapshot_entry{of, placed, aim, json_bool(entry, "shooter").value_or(false)});
        }
    }
    return entries;
}

std::vector<int> golden_flags_out(const json& shot) {
    std::vector<int> out;
    if (const json* listed = json_array(shot, "flags_out")) {
        for (const json& hole : *listed) {
            out.push_back(hole.get<int>());
        }
    }
    return out;
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
    const game_state state = teed_hole(golden_content(), 0);
    shot_setup input = tee_shot(state, "driver", 0.9f);
    input.shot_at = 4000;
    const shot_result first = simulate(state, input);

    // A second state, built separately and played in before the shot.
    game_state other = teed_hole(golden_content(), 0);
    hit_selected_club(other);
    play_out(other, 1.0f / 60.0f);
    const shot_result second = simulate(other, input);

    CHECK(first.duration > 0.0f);
    CHECK(first.trajectory.size() > 2U);
    CHECK(!first.events.empty());
    CHECK(same_result(first, second));
}

TEST_CASE("playback ends at the simulated rest whatever the frame rate") {
    const game_state start = teed_hole(golden_content(), 0);
    shot_setup input = tee_shot(start, "driver", 0.8f);
    input.aim_angle += glm::radians(-45.0f);
    const shot_result result = simulate(start, input);
    REQUIRE(!result.holed);
    REQUIRE(count_events(result, shot_event_kind::tree_hit) + count_events(result, shot_event_kind::leaves) > 0);

    std::vector<audio_event_type> expected;
    for (const shot_event& event : result.events) {
        expected.push_back(event.kind == shot_event_kind::land ? audio_event_type::ball_land : audio_event_type::ball_hit);
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
        CHECK(state.ball->position == result.rest_position);
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
        const game_state state = teed_hole(content, static_cast<std::size_t>(*json_int(shot, "hole")));
        shot_setup input = tee_shot(state, *json_string(shot, "club"), *json_float(shot, "backswing"));
        // A putt from short of the pin, on the line from the tee.
        if (const std::optional<float> putt = json_float(shot, "putt_from_pin_meters")) {
            const glm::vec3 pin = current_shot_course(state).hole.pin;
            input.ball_start = resting_on_terrain(state, pin - yaw_direction(state.player.yaw) * *putt);
            input.aim_angle = yaw_towards(input.ball_start, pin);
        }
        input.aim_angle += glm::radians(*json_float(shot, "aim_offset_degrees"));
        input.shot_at = shot.value("shot_at", std::int64_t{0});

        std::vector<bool> flags_in(state.area.cups.size(), true);
        for (const int out : golden_flags_out(shot)) {
            flags_in[static_cast<std::size_t>(out)] = false;
        }
        shot_course course = make_shot_course(state.area, state.ball->hole, course_weather_at(state, input.shot_at),
                                              state.course, flags_in, state.tuning);
        const shot_record record{input, shot.value("seed", std::uint64_t{0}),
                                 golden_snapshot(shot, state.area, input.ball_start, input.aim_angle,
                                                 state.tuning.scale.ball_physics_radius_meters)};
        const shot_result result = simulate_struck_shot(record, golfer_profile{}, course, state.tuning, state.clubs);
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
        tree_hits += count_events(result, shot_event_kind::tree_hit) + count_events(result, shot_event_kind::leaves);
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

    ball_state ball = still_ball(state);
    ball.position = glm::vec3(0.0f, pond.water_level + 3.0f, 40.0f);
    ball.velocity = glm::vec3(0.0f, -4.0f, 1.0f);
    const shot_result result = simulate_ball(ball, course, state.tuning);

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
    CHECK(simulate_ball(ball, course, state.tuning).penalty_strokes == 0);
}

TEST_CASE("from sand a driver goes nowhere, an iron picks the ball clean and a sand wedge explodes it out") {
    game_state state = started_hole();
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 300.0f), 30.0f);
    hole.material_zones = {circle_zone(material_zone_type::bunker, glm::vec3(0.0f, 0.0f, 40.0f), 4.0f)};
    play_hole(state, hole);
    state.tuning.wind = wind_tuning{};
    const auto hit = [&](const std::string& club, const glm::vec3& from) {
        shot_setup input;
        input.ball_start = resting_on_terrain(state, from);
        input.aim_angle = yaw_towards(from, glm::vec3(0.0f, 0.0f, 300.0f));
        input.club_id = club;
        input = stocked(input, state, 1.0f);
        return simulate_shot(input, current_shot_course(state), state.tuning, state.clubs);
    };
    const auto carry = [&](const std::string& club, const glm::vec3& from) {
        const shot_result result = hit(club, from);
        const auto landing = std::find_if(result.events.begin(), result.events.end(),
                                          [](const shot_event& event) { return event.kind == shot_event_kind::land; });
        const float time = landing != result.events.end() ? landing->time : result.duration;
        return horizontal_distance(shot_position_at(result, time), from);
    };
    const glm::vec3 sand(0.0f, 0.0f, 40.0f);  // off the tee box, which is a tee lie
    const glm::vec3 fairway(0.0f, 0.0f, 20.0f);
    REQUIRE(lie_at(state.area, sand) == ball_lie::bunker);
    REQUIRE(lie_at(state.area, fairway) == ball_lie::fairway);

    // The driver's low face ploughs the sand on.
    CHECK(horizontal_distance(hit("driver", sand).rest_position, sand) < 10.0f);
    // An iron comes down on the ball before the sand.
    CHECK(carry("seven_iron", sand) > 0.85f * carry("seven_iron", fairway));
    // The sand wedge's open face throws the ball out on the sand, about as
    // far as a tour player's longest explosion.
    const json reference = *parse_json(*read_text_file(fixture_root() + "/reference/lies.json"));
    const glm::vec2 range = *json_vec2(*json_object(*json_object(reference, "sand"), "explosion_carry_share"), "range");
    const float exploded = carry("sand_wedge", sand) / carry("sand_wedge", fairway);
    CHECK(exploded > range.x);
    CHECK(exploded < range.y);
}

TEST_CASE("from sand an open face gets the ball higher and shorter, with little spin") {
    const game_state state = started_hole();
    club_definition wedge = club_named(state, "sand_wedge");
    REQUIRE(wedge.spec.sand_face_open_degrees > 0.0f);
    const ball_on_lie sand{ball_lie::bunker, {}, 0.0f};
    const launch_conditions open = club_strike(wedge, stock_swing(wedge, state.tuning, 1.0f, ball_lie::bunker), sand, state.tuning).launch;
    wedge.spec.sand_face_open_degrees = 0.0f;
    const launch_conditions square = club_strike(wedge, stock_swing(wedge, state.tuning, 1.0f, ball_lie::bunker), sand, state.tuning).launch;
    CHECK(open.launch_angle > square.launch_angle);
    CHECK(open.ball_speed < 0.6f * square.ball_speed);
    CHECK(open.spin < 0.5f * square.spin);
}

TEST_CASE("rough's grass halves the spin, a flier, and closes the face, more with more loft and a longer swing") {
    const game_state state = started_hole();
    const json reference = *parse_json(*read_text_file(fixture_root() + "/reference/lies.json"));
    const float flier = *json_float(*json_object(*json_object(reference, "rough"), "spin_share_flier"), "value");
    const ball_on_lie fairway{ball_lie::fairway, {}, 0.0f};
    const ball_on_lie rough{ball_lie::rough, {}, 0.0f};
    const auto strike = [&](const char* club, const float power, const ball_on_lie& on) {
        return club_strike(club_named(state, club), stock_swing(club_named(state, club), state.tuning, power, on.lie), on, state.tuning).launch;
    };
    const launch_conditions clean = strike("seven_iron", 1.0f, fairway);
    const launch_conditions grassy = strike("seven_iron", 1.0f, rough);
    CHECK(near(grassy.spin / clean.spin, flier, 0.1f));
    CHECK(grassy.ball_speed < clean.ball_speed);
    // Closed: it starts left of the aim, for a right-hander.
    CHECK(grassy.direction > 0.0f);
    CHECK(strike("sand_wedge", 1.0f, rough).direction > strike("five_iron", 1.0f, rough).direction);
    CHECK(strike("seven_iron", 0.5f, rough).direction < grassy.direction);
}

TEST_CASE("an unknown club leaves the ball where it is") {
    const game_state state = teed_hole(golden_content(), 0);
    const shot_result result = simulate(state, tee_shot(state, "no_such_club", 1.0f));

    CHECK(result.rest_position == state.ball->position);
    CHECK(result.duration == 0.0f);
    CHECK(!result.holed);
}

TEST_CASE("a headwind shortens a drive, a tailwind lengthens it, and a crosswind moves it sideways") {
    const game_state state = teed_hole(golden_content(), 0);
    const shot_setup drive = tee_shot(state, "driver", 1.0f);
    const glm::vec3 forward = yaw_direction(drive.aim_angle);
    const auto carry_in = [&](const glm::vec3& wind) {
        shot_course course = current_shot_course(state);
        course.weather.wind = wind;
        return simulate_shot(drive, course, state.tuning, state.clubs);
    };
    const shot_result calm = carry_in(glm::vec3(0.0f));
    const float still = glm::dot(calm.rest_position - drive.ball_start, forward);
    const float into = glm::dot(carry_in(-forward * 8.0f).rest_position - drive.ball_start, forward);
    const float with = glm::dot(carry_in(forward * 8.0f).rest_position - drive.ball_start, forward);
    CHECK(into < still);
    CHECK(with > still);

    const glm::vec3 left = yaw_left(forward);
    const shot_result across = carry_in(left * 8.0f);
    CHECK(glm::dot(across.rest_position - calm.rest_position, left) > 1.0f);
}

TEST_CASE("a putt barely feels the wind a drive flies in") {
    const game_state state = teed_hole(golden_content(), 2);
    shot_setup putt = tee_shot(state, "putter", 0.4f);
    const glm::vec3 left = yaw_left(yaw_direction(putt.aim_angle));
    shot_course course = current_shot_course(state);
    const shot_result calm = simulate_shot(putt, course, state.tuning, state.clubs);
    course.weather.wind = left * 8.0f;
    const shot_result windy = simulate_shot(putt, course, state.tuning, state.clubs);
    CHECK(horizontal_distance(calm.rest_position, windy.rest_position) < 0.05f);
}

TEST_CASE("wet ground stops a drive sooner, and rain is no drag in the air") {
    game_state state = started_hole();
    // A wide fairway all the way, so the drive lands and runs on it.
    play_hole(state, straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 500.0f), 120.0f));
    state.tuning.wind = wind_tuning{};
    shot_setup drive;
    drive.ball_start = resting_on_terrain(state, glm::vec3(0.0f, 0.0f, 20.0f));
    drive.aim_angle = yaw_towards(drive.ball_start, glm::vec3(0.0f, 0.0f, 500.0f));
    drive.club_id = "driver";
    drive = stocked(drive, state, 1.0f);
    REQUIRE(lie_at(state.area, drive.ball_start) == ball_lie::fairway);
    const auto travel = [&](const float rain, const float wetness) {
        shot_course course = current_shot_course(state);
        course.weather.rain = rain;
        course.weather.wetness = wetness;
        const shot_result result = simulate_shot(drive, course, state.tuning, state.clubs);
        return horizontal_distance(result.rest_position, drive.ball_start);
    };
    const float dry = travel(0.0f, 0.0f);
    CHECK(travel(0.0f, 1.0f) < dry - 5.0f);
    // Rain acts through the wet face, ball and ground, not as extra drag.
    CHECK(travel(1.0f, 0.0f) == dry);
}

TEST_CASE("a wet lie's water film takes backspin off the club, and a teed ball has none") {
    const game_state state = started_hole();
    const club_definition& club = club_named(state, "pitching_wedge");
    REQUIRE(state.tuning.lies.wet_grip_loss > 0.0f);
    const auto spin = [&](const ball_lie lie, const float wetness) {
        return club_strike(club, stock_swing(club, state.tuning, 1.0f, lie), ball_on_lie{lie, {}, wetness}, state.tuning).launch.spin;
    };
    REQUIRE(spin(ball_lie::fairway, 0.0f) > 0.0f);
    CHECK(spin(ball_lie::fairway, 1.0f) < spin(ball_lie::fairway, 0.0f));
    CHECK(spin(ball_lie::rough, 1.0f) < spin(ball_lie::rough, 0.0f));
    CHECK(spin(ball_lie::tee, 1.0f) == spin(ball_lie::tee, 0.0f));
}

TEST_CASE("a shot flies the same whichever way it is aimed") {
    const game_state state = started_hole();
    const club_definition& club = club_named(state, "five_iron");
    const ball_on_lie clean;
    const ball_state north = launch_ball(club, stock_swing(club, state.tuning, 0.7f, clean.lie), clean, state.tuning);
    const ball_state east = launch_ball(club, stock_swing(club, state.tuning, 0.7f, clean.lie, glm::radians(90.0f)), clean, state.tuning);

    // Rotate the east shot back onto north and compare.
    const glm::vec3 east_velocity_as_north(-east.velocity.z, east.velocity.y, east.velocity.x);
    const glm::vec3 east_spin_as_north(-east.spin.z, east.spin.y, east.spin.x);
    CHECK(near(east_velocity_as_north, north.velocity, 0.001f));
    CHECK(near(east_spin_as_north, north.spin, 0.001f));
}

TEST_CASE("a setup is held within its club's limits") {
    game_state state = started_hole();
    const auto speed = [&](const club_definition& club, const float backswing) {
        return glm::length(launch_ball(club, stock_swing(club, state.tuning, backswing), ball_on_lie{}, state.tuning).velocity);
    };
    const club_definition& club = club_named(state, "seven_iron");
    const setup_limits limits = setup_limits_for(club, state.tuning.setup);
    // No shorter than one step; past full an overswing, as far as its limit.
    CHECK(speed(club, 0.0f) == speed(club, limits.backswing_step));
    CHECK(speed(club, 0.7f) > speed(club, limits.backswing_step));
    CHECK(limits.backswing_max > 1.0f);
    CHECK(speed(club, limits.backswing_max) > speed(club, 1.0f));
    CHECK(speed(club, 3.0f) == speed(club, limits.backswing_max));
    // A putter has no overswing.
    const club_definition& putter = putter_of(state);
    CHECK(speed(putter, 1.5f) == speed(putter, 1.0f));
    // The face opens and closes as far as the club's spec says.
    const auto start = [&](const float face_open) {
        shot_setup setup = stock_swing(club, state.tuning);
        setup.face_open = face_open;
        return club_strike(club, setup, ball_on_lie{}, state.tuning).launch.direction;
    };
    CHECK(start(glm::radians(90.0f)) == start(glm::radians(club.spec.max_face_open_degrees)));
    CHECK(start(glm::radians(-90.0f)) == start(glm::radians(-club.spec.max_face_closed_degrees)));
    CHECK(speed(club, std::nanf("")) == speed(club, limits.backswing_step));
}

TEST_CASE("more loft launches the ball higher") {
    const game_state state = started_hole();
    const club_definition& putter = putter_of(state);
    const club_definition& lob = club_named(state, "lob_wedge");
    const ball_state putt = launch_ball(putter, stock_swing(putter, state.tuning), ball_on_lie{}, state.tuning);
    const ball_state wedge = launch_ball(lob, stock_swing(lob, state.tuning), ball_on_lie{}, state.tuning);

    CHECK(wedge.velocity.y / horizontal_speed(wedge.velocity) > putt.velocity.y / horizontal_speed(putt.velocity));
}

namespace {
// A flat green 60 m long, its cup at its far end, in still air.
constexpr float green_length = 60.0f;

void play_flat_green(game_state& state, const float rise = 0.0f) {
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, rise, green_length), 40.0f);
    hole.material_zones.push_back(
        circle_zone(material_zone_type::green, glm::vec3(0.0f, 0.0f, 0.5f * green_length), 80.0f));
    play_hole(state, hole);
    still_air(state);
}

// A ball on the ground at `position`, moving at `velocity` and rolling.
ball_state rolling_ball(const game_state& state, const glm::vec3& position, const glm::vec3& velocity) {
    ball_state ball = resting_ball(state, position);
    ball.velocity = velocity;
    ball.spin = glm::cross(world_up, velocity) / ball.radius;
    return ball;
}

// Backspin of `rpm` for a ball moving along `velocity`.
glm::vec3 backspin(const glm::vec3& velocity, const float rpm) {
    return safe_normalize(glm::cross(horizontal(velocity), world_up), glm::vec3(1.0f, 0.0f, 0.0f)) * (rpm * glm::two_pi<float>() / 60.0f);
}

const cup& target_cup(const game_state& state) {
    return state.area.cups[state.ball->hole];
}

// A ball rolling at `speed` along +z on the flat green, from just short of
// its cup's rim and `offset` to the side of its centre.
shot_result roll_at_cup(const game_state& state, const float speed, const float offset) {
    const cup& hole = target_cup(state);
    const float short_of = std::sqrt(std::max(0.0f, hole.radius * hole.radius - offset * offset)) + 0.03f;
    const ball_state ball =
        rolling_ball(state, hole.centre + glm::vec3(offset, 0.0f, -short_of), glm::vec3(0.0f, 0.0f, speed));
    return simulate_ball(ball, current_shot_course(state), state.tuning);
}

bool has_event(const shot_result& result, const shot_event_kind kind) {
    return std::any_of(result.events.begin(), result.events.end(), [kind](const shot_event& event) { return event.kind == kind; });
}
}

TEST_CASE("Penner's green: restitution falls with impact speed, the turf's give grows with speed and steepness") {
    CHECK(green_restitution(2.0f) > green_restitution(10.0f));
    CHECK(green_restitution(10.0f) > green_restitution(20.0f));
    CHECK(near(green_restitution(25.0f), 0.12f));
    CHECK(near(glm::degrees(green_contact_angle(18.6f, glm::radians(44.4f))), 15.4f, 0.01f));
    CHECK(green_contact_angle(25.0f, glm::radians(50.0f)) > green_contact_angle(15.0f, glm::radians(50.0f)));
    CHECK(green_contact_angle(20.0f, glm::radians(60.0f)) > green_contact_angle(20.0f, glm::radians(30.0f)));
}

TEST_CASE("a ball rolling off a stimpmeter at 1.83 m/s rolls the green's reading in feet") {
    for (const float stimp : {8.0f, 10.0f, 12.0f}) {
        game_state state = started_hole();
        state.course.green_stimp_feet = stimp;
        play_flat_green(state);
        const glm::vec3 from(0.0f, 0.0f, 10.0f);
        const ball_state ball = rolling_ball(state, from, glm::vec3(0.0f, 0.0f, 1.83f));
        const shot_result rolled = simulate_ball(ball, current_shot_course(state), state.tuning);
        CHECK(near(horizontal_distance(rolled.rest_position, ball.position), stimp * 0.3048f, 0.03f * stimp * 0.3048f));
    }
}

TEST_CASE("a green holds a still ball on a slope gentler than its stimp holds, and not on a steeper one") {
    // A stimp-10 green holds a ball up to about 4.4 degrees.
    for (const float degrees : {3.0f, 6.0f}) {
        game_state state = started_hole();
        state.course.green_stimp_feet = 10.0f;
        play_flat_green(state, green_length * std::tan(glm::radians(degrees)));
        const ball_state ball = resting_ball(state, glm::vec3(0.0f, 0.0f, 20.0f));
        const shot_result left = simulate_ball(ball, current_shot_course(state), state.tuning);
        const float moved = horizontal_distance(left.rest_position, ball.position);
        if (degrees < 4.4f) {
            CHECK(moved < 0.01f);
        } else {
            CHECK(moved > 1.0f);
            CHECK(left.rest_position.z < ball.position.z);  // downhill
        }
    }
}

TEST_CASE("a soaked green rolls slower, by its surface's wet stimp") {
    game_state state = started_hole();
    play_flat_green(state);
    const ball_state ball = rolling_ball(state, glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f, 0.0f, 1.83f));
    const auto roll = [&](const float wetness) {
        shot_course course = current_shot_course(state);
        course.weather.wetness = wetness;
        return horizontal_distance(simulate_ball(ball, course, state.tuning).rest_position, ball.position);
    };
    const float share = roll(1.0f) / roll(0.0f);
    CHECK(near(share, state.tuning.surfaces.green.wet_stimp_scale, 0.03f));
}

TEST_CASE("a wedge with heavy backspin checks on a green and can spin back; a low-spinning drive skips on") {
    game_state state = started_hole();
    play_flat_green(state);
    const auto landing = [&](const float speed, const float descent_degrees, const float rpm) {
        const glm::vec3 velocity = glm::vec3(0.0f, -std::sin(glm::radians(descent_degrees)), std::cos(glm::radians(descent_degrees))) * speed;
        ball_state ball = resting_ball(state, glm::vec3(0.0f, 0.0f, 10.0f));
        ball.position.y += 0.05f;
        ball.velocity = velocity;
        ball.spin = backspin(velocity, rpm);
        const shot_result result = simulate_ball(ball, current_shot_course(state), state.tuning);
        return result.rest_position.z - ball.position.z;
    };
    // A tour wedge comes down at about 50 degrees and 20 m/s with 9000 rpm left.
    const float wedge = landing(20.0f, 50.0f, 9000.0f);
    CHECK(wedge < 2.0f);
    CHECK(wedge < landing(20.0f, 50.0f, 0.0f) - 3.0f);
    // A drive at 38 degrees and 25 m/s with 2500 rpm runs out.
    CHECK(landing(25.0f, 38.0f, 2500.0f) > 10.0f);
}

TEST_CASE("a putt that rolls off the green is caught by the rough") {
    game_state state = started_hole();
    still_air(state);
    // The first rough beside the tee, away from the fairway.
    glm::vec3 start = state.holes[state.ball->hole].tee_position;
    while (sample_area(state.area, start).material != terrain_material::rough) {
        start.x += 0.5f;
    }
    start.x += 1.0f;
    REQUIRE(sample_area(state.area, start).material == terrain_material::rough);
    const ball_state ball = rolling_ball(state, start, glm::vec3(3.0f, 0.0f, 0.0f));
    const shot_result putt = simulate_ball(ball, current_shot_course(state), state.tuning);
    CHECK(horizontal_distance(putt.rest_position, ball.position) < 1.5f);
}

TEST_CASE("a ball on a slope keeps its velocity along the surface") {
    game_state state = started_hole();
    play_hole(state, straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 5.0f, 10.0f), 8.0f));
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
    ball_state ball = still_ball(state);
    ball.position = glm::vec3(0.0f, 4.05f, 5.0f);
    ball.velocity = glm::vec3(0.0f, -2.0f, 0.0f);
    const shot_step step = step_once(state, ball, 0.05f);
    const terrain_sample ground = sample_area(state.area, glm::vec3(0.0f, 0.0f, 5.0f));
    CHECK(near(glm::dot(step.ball.position - ground.point, ground.normal), ball.radius, 0.0001f));
    CHECK(step.ball.velocity.y > 0.0f);  // it bounces
    CHECK(step.landed);
}

TEST_CASE("the ball bounces off a tree trunk where the tree stands on the terrain") {
    game_state state = started_hole();
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 6.0f, 20.0f), 8.0f);
    hole.trees = {tree_instance{glm::vec3(12.0f, 0.0f, 10.0f), tree_shape{0.5f, 2.0f, 1.0f, 2.0f}}};
    play_hole(state, hole);
    still_air(state);

    REQUIRE(state.static_anchors.trees.size() == 1U);
    const glm::vec3 base = state.static_anchors.trees.front().base;
    CHECK(near(base.x, 12.0f));
    CHECK(near(base.z, 10.0f));
    CHECK(base.y > 1.0f);

    ball_state ball = still_ball(state);
    ball.radius = 0.1f;
    ball.position = base + glm::vec3(0.35f, 0.8f, 0.0f);
    ball.velocity = glm::vec3(-3.0f, 0.0f, 0.0f);
    const shot_step step = step_once(state, ball, 0.016f);

    CHECK(step.ball.position.x >= 12.0f + 0.5f + 0.1f - 0.0001f);
    CHECK(step.ball.velocity.x > 0.0f);
    CHECK(step.hit == shot_event_kind::tree_hit);
}

TEST_CASE("a low shot through the middle of a canopy is slowed far more than one clipping its edge, and the same dice replay it") {
    game_state state = started_hole();
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 60.0f), 30.0f);
    hole.trees = {tree_instance{glm::vec3(0.0f, 0.0f, 20.0f), tree_shape{0.3f, 2.0f, 3.0f, 6.0f}}};
    play_hole(state, hole);
    still_air(state);
    const glm::vec3 base = state.static_anchors.trees.front().base;
    const auto through = [&](const float side, const std::uint64_t seed) {
        shot_course course = current_shot_course(state);
        course.seed = seed;
        ball_state ball = still_ball(state);
        // Level, a metre into the canopy's height, just short of it.
        ball.position = base + glm::vec3(side, 3.0f, -4.0f);
        ball.velocity = glm::vec3(0.0f, 0.0f, 30.0f);
        terrain_sample ground = sample_area(state.area, ball.position);
        bool rustled = false;
        for (int i = 0; i < 60; ++i) {
            const shot_step step = step_shot(ball, ground, course, state.tuning, shot_step_seconds, i);
            rustled = rustled || step.through_leaves;
            ball = step.ball;
            ground = step.ground;
        }
        CHECK(rustled);
        return ball;
    };
    const ball_state middle = through(0.0f, 7);
    const ball_state edge = through(2.4f, 7);
    CHECK(horizontal_speed(middle.velocity) < 0.5f * horizontal_speed(edge.velocity));
    CHECK(horizontal_speed(edge.velocity) > 15.0f);
    // The same dice turn it the same way, others otherwise.
    const ball_state again = through(0.0f, 7);
    CHECK(again.position == middle.position);
    CHECK(again.velocity == middle.velocity);
    CHECK(through(0.0f, 8).position != middle.position);
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
        state->tuning.ball.water_friction = 0.0f;
        // On the pond's bed, under its water (on the grass, for the dry hole), rolling.
        const ball_state ball = rolling_ball(*state, glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(6.0f, 0.0f, 0.0f));
        speeds.push_back(horizontal_speed(step_once(*state, ball, 0.016f).ball.velocity));
    }
    CHECK(speeds[0] < speeds[1]);
}

TEST_CASE("a centred putt drops below Holmes's capture speed and runs over the real cup above it") {
    game_state state = started_hole();
    state.tuning.cup.leniency = 1.0f;
    play_flat_green(state);
    REQUIRE(near(target_cup(state).radius, 0.054f, 1.0e-4f));
    // Holmes's cup has no flagstick in it.
    state.flags_in[state.ball->hole] = false;
    // Holmes (1991): dead centre a real ball drops below about 1.31 m/s.
    CHECK(roll_at_cup(state, 1.1f, 0.0f).holed);
    CHECK(!roll_at_cup(state, 1.6f, 0.0f).holed);
    // A bigger cup (cup_leniency) takes a firmer putt.
    game_state lenient = started_hole();
    play_flat_green(lenient);
    lenient.flags_in[lenient.ball->hole] = false;
    CHECK(lenient.tuning.cup.leniency > 1.0f);
    CHECK(roll_at_cup(lenient, 1.8f, 0.0f).holed);
}

TEST_CASE("off centre the cup takes only a slower putt: a firm one lips out") {
    game_state state = started_hole();
    play_flat_green(state);
    const float radius = target_cup(state).radius;
    CHECK(roll_at_cup(state, 1.5f, 0.0f).holed);
    const shot_result lipped = roll_at_cup(state, 1.5f, 0.8f * radius);
    CHECK(!lipped.holed);
    CHECK(has_event(lipped, shot_event_kind::cup_rattle));
    CHECK(roll_at_cup(state, 0.6f, 0.8f * radius).holed);
}

TEST_CASE("a lip-out horseshoes at most three quarters round the cup: it never rides round it") {
    game_state state = started_hole();
    play_flat_green(state);
    const cup& hole = target_cup(state);
    float worst = 0.0f;
    int lipped = 0;
    for (float speed = 0.6f; speed <= 2.5f; speed += 0.1f) {
        for (float offset = 0.0f; offset <= 1.2f * hole.radius; offset += 0.1f * hole.radius) {
            const shot_result result = roll_at_cup(state, speed, offset);
            if (result.holed) {
                continue;
            }
            lipped += has_event(result, shot_event_kind::cup_rattle) ? 1 : 0;
            // How far round the cup's centre it goes, one way, while on or
            // near the rim (a ball bouncing back off the far wall goes nowhere round it).
            float round = 0.0f;
            float most = 0.0f;
            std::optional<float> bearing;
            for (const glm::vec3& point : result.trajectory) {
                const float out = horizontal_distance(point, hole.centre);
                if (out < 0.5f * hole.radius || out > 2.0f * hole.radius) {
                    bearing.reset();
                    continue;
                }
                const glm::vec3 from = point - hole.centre;
                const float now = std::atan2(from.x, from.z);
                if (bearing) {
                    round += wrap_angle(now - *bearing);
                    most = std::max(most, std::abs(round));
                }
                bearing = now;
            }
            worst = std::max(worst, most);
        }
    }
    // Some do lip out; rolling straight across, a ball goes about 150
    // degrees round the band, and the lip curls one further, at most as far
    // as real golf's rare 270.
    CHECK(lipped > 0);
    CHECK(worst <= glm::radians(270.0f));
}

TEST_CASE("a ball stopping just short of the rim stays out, and one rolling in rests in the cup") {
    game_state state = started_hole();
    play_flat_green(state);
    const cup& hole = target_cup(state);
    const shot_result outside =
        simulate_ball(resting_ball(state, hole.centre + glm::vec3(hole.radius + 0.03f, 0.0f, 0.0f)), current_shot_course(state),
                      state.tuning);
    CHECK(!outside.holed);
    const shot_result holed = roll_at_cup(state, 0.8f, 0.0f);
    REQUIRE(holed.holed);
    CHECK(holed.rest_position.y < hole.centre.y);
    CHECK(horizontal_distance(holed.rest_position, hole.centre) < hole.radius);
}

TEST_CASE("centred putts too firm for the cup mostly drop off the flagstick; with the flag out they run over") {
    game_state state = started_hole();
    play_flat_green(state);
    REQUIRE(state.flags_in[state.ball->hole]);
    int firm = 0;
    int dropped = 0;
    for (float speed = 2.2f; speed <= 3.4f; speed += 0.1f) {
        state.flags_in[state.ball->hole] = false;
        if (roll_at_cup(state, speed, 0.0f).holed) {
            continue;
        }
        ++firm;
        state.flags_in[state.ball->hole] = true;
        const shot_result in = roll_at_cup(state, speed, 0.0f);
        CHECK(has_event(in, shot_event_kind::flag_hit));
        dropped += in.holed ? 1 : 0;
    }
    REQUIRE(firm >= 8);
    // As Pelz's flag-in putts did (tests/fixtures/reference/cup.json).
    CHECK(dropped * 2 > firm);
}

TEST_CASE("a slow putt hitting the flagstick still holes, with no penalty") {
    game_state state = started_hole();
    play_flat_green(state);
    const shot_result result = roll_at_cup(state, 1.0f, 0.0f);
    CHECK(result.holed);
    CHECK(result.penalty_strokes == 0);
}

TEST_CASE("a putt into the flagstick lying on the green bounces off it, with no penalty") {
    game_state state = started_hole();
    play_flat_green(state);
    state.flags_in[state.ball->hole] = false;
    const std::vector<solid> sticks = flagstick_solids(state.area.cups, state.flags_in, state.tuning);
    const auto lying = std::find_if(sticks.begin(), sticks.end(), [](const solid& stick) { return stick.shape == solid_shape::wall; });
    REQUIRE(lying != sticks.end());
    // Across its middle, from a metre to one side.
    const glm::vec3 middle = 0.5f * (lying->base + lying->end);
    const ball_state ball = rolling_ball(state, middle + glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 0.0f, 2.0f));
    const shot_result result = simulate_ball(ball, current_shot_course(state), state.tuning);
    CHECK(has_event(result, shot_event_kind::flag_hit));
    CHECK(result.penalty_strokes == 0);
    CHECK(result.rest_position.z < middle.z);
}

TEST_CASE("a steep ball landing in the cup dunks; a fast low one skips over") {
    game_state state = started_hole();
    play_flat_green(state);
    const auto fall_in = [&](const float forward, const float down) {
        const glm::vec3 centre = target_cup(state).centre;
        const float height = 0.3f;
        const float seconds = (std::sqrt(down * down + 2.0f * gravity_meters_per_second2 * height) - down) / gravity_meters_per_second2;
        ball_state ball = still_ball(state);
        ball.position = centre + glm::vec3(0.0f, ball.radius + height, -forward * seconds);
        ball.velocity = glm::vec3(0.0f, -down, forward);
        return simulate_ball(ball, current_shot_course(state), state.tuning);
    };
    CHECK(fall_in(3.0f, 5.0f).holed);
    state.flags_in[state.ball->hole] = false;  // nothing to stop it
    CHECK(!fall_in(10.0f, 0.5f).holed);
}

TEST_CASE("a ball resting in another green's cup is lifted out, dropped beside it and not holed") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(tee_up(state, 0, false));
    REQUIRE(state.area.cups.size() >= 2U);
    still_air(state);
    const cup& other = state.area.cups[1];
    const ball_state ball = rolling_ball(state, other.centre + glm::vec3(-0.15f, 0.0f, 0.0f), glm::vec3(0.6f, 0.0f, 0.0f));
    const shot_result result = simulate_ball(ball, current_shot_course(state), state.tuning);
    CHECK(!result.holed);
    const float drop = state.tuning.cup.wrong_cup_drop_distance_meters / state.tuning.scale.meters_per_world_unit;
    CHECK(near(horizontal_distance(result.rest_position, other.centre), drop, 0.01f));
    CHECK(result.rest_position.x < other.centre.x);  // on the side it came in from
}

TEST_CASE("people walk under a tree's leaves but not through its trunk") {
    game_state state = started_hole();
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 60.0f), 30.0f);
    play_hole(state, hole);
    const glm::vec3 ahead = yaw_direction(state.player.yaw);
    hole.trees = {tree_instance{state.player.position + ahead * 4.0f + yaw_left(ahead) * 1.5f, tree_shape{0.3f, 1.0f, 3.0f, 4.0f}}};
    play_hole(state, hole);
    const glm::vec3 start = state.player.position;
    game_input walk;
    walk.forward_held = true;
    for (int i = 0; i < 30; ++i) {
        update_game(state, walk, 0.02f);
    }
    CHECK(!state.player.against_obstacle);
    CHECK(horizontal_distance(state.player.position, start) > 0.9f * state.tuning.player.walk_speed * 0.6f);
}

namespace {
// A putt with the shipped putter and tuning, from `distance` short of `pin` on
// the line from the tee, aimed at the cup.
shot_result putt(const game_state& state, const glm::vec3& pin, const float distance, const float power) {
    shot_setup input;
    input.ball_start = resting_on_terrain(state, pin - glm::vec3(0.0f, 0.0f, distance));
    input.aim_angle = yaw_towards(input.ball_start, pin);
    input.club_id = "putter";
    input = stocked(input, state, power);
    return simulate_shot(input, current_shot_course(state), state.tuning, state.clubs);
}
}

TEST_CASE("every club carries further the longer the swing") {
    game_state state = make_game_state(shipped_content(), save_data{});
    // All green, so the putter rolls as it does on a green.
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 600.0f), 40.0f);
    hole.material_zones.push_back(circle_zone(material_zone_type::green, glm::vec3(0.0f, 0.0f, 300.0f), 400.0f));
    play_hole(state, hole);
    state.tuning.wind = wind_tuning{};
    const auto carry = [&state](const std::string& club, const float power) {
        shot_setup input;
        input.ball_start = resting_on_terrain(state, glm::vec3(0.0f));
        input.aim_angle = yaw_towards(input.ball_start, glm::vec3(0.0f, 0.0f, 600.0f));
        input.club_id = club;
        input = stocked(input, state, power);
        const shot_result result = simulate(state, input);
        // Where it first comes down (a putt never leaves the green: where it rests).
        const auto landing = std::find_if(result.events.begin(), result.events.end(),
                                          [](const shot_event& event) { return event.kind == shot_event_kind::land; });
        const glm::vec3 down = landing != result.events.end() ? shot_position_at(result, landing->time) : result.rest_position;
        return horizontal_distance(down, input.ball_start);
    };
    // Every club carries strictly further the longer the swing.
    for (const club_definition& club : state.clubs) {
        float previous = 0.0f;
        for (const float power : {0.25f, 0.5f, 0.75f, 1.0f}) {
            const float distance = carry(club.id, power);
            CHECK(distance > previous);
            previous = distance;
        }
    }
}

TEST_CASE("a well-paced putt drops, a hard one stays out and a short one stops at the lip") {
    constexpr float power_step = 0.005f;
    // A flat green, one running downhill to the cup and one uphill.
    for (const float pin_height : {0.0f, -1.0f, 1.0f}) {
        game_state state = started_hole();
        const float least = setup_limits_for(putter_of(state), state.tuning.setup).backswing_step;
        const glm::vec3 pin(0.0f, pin_height, 20.0f);
        hole_data hole = straight_hole(glm::vec3(0.0f), pin, 20.0f);
        hole.material_zones.push_back(circle_zone(material_zone_type::green, pin, 12.0f));
        play_hole(state, hole);
        state.flags_in[state.ball->hole] = false;
        const float radius = state.area.cups[0].radius;
        for (const float distance : {1.0f, 2.0f, 3.0f, 5.0f, 8.0f}) {
            // The softest putt that holes, and how many steps of the meter hole.
            float first = -1.0f;
            float last = -1.0f;
            int holing = 0;
            for (float power = least; power <= 1.0f; power += power_step) {
                if (putt(state, pin, distance, power).holed) {
                    first = first < 0.0f ? power : first;
                    last = power;
                    ++holing;
                }
            }
            REQUIRE(first > 0.0f);
            // A short putt can be hit softer than it needs (downhill this
            // steep, the slope alone runs the softest putt down to the cup).
            if (pin_height >= 0.0f) {
                CHECK(first > least);
            }
            // Not pixel-perfect: a decent swing has some room.
            CHECK(holing * power_step >= 0.04f);

            // Firmer than any that holes runs past, where a full stroke is firmer.
            const float firm = last + 0.05f;
            if (firm <= 1.0f) {
                const shot_result hard = putt(state, pin, distance, firm);
                CHECK(!hard.holed);
                // Past, or lipped out and spun back: never in.
                CHECK(horizontal_distance(hard.rest_position, pin) > radius);
            }

            if (first - power_step >= least) {
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
    state.ball->strokes = 2;
    const shot_course course = current_shot_course(state);
    const glm::vec3 beside = course.hole.pin + glm::vec3(course.area.cups[course.hole.index].radius + 2.0f, 0.0f, 0.0f);
    play_shot(state, simulate_ball(resting_ball(state, beside), course, state.tuning));

    play_out(state, 0.016f);

    CHECK(state.mode == game_mode::walking);
    CHECK(state.ball->strokes == 2);
    CHECK(!hole_played(state.round, 0));
}

TEST_CASE("a holed shot completes the hole once it has played") {
    game_state state = started_hole();
    state.ball->strokes = 2;
    const shot_course course = current_shot_course(state);
    play_shot(state, simulate_ball(resting_ball(state, course.hole.pin), course, state.tuning));

    const std::vector<audio_event> played = play_out(state, 0.016f);

    CHECK(count_audio(played, audio_event_type::ball_cup) == 1);
    CHECK(hole_played(state.round, 0));
    CHECK(*state.round.strokes[0] == 2);
    CHECK(state.round.current_hole_index == 1);
    REQUIRE(state.ball.has_value());
    CHECK(state.ball->place == ball_place::in_cup);
    CHECK(state.mode == game_mode::walking);
    CHECK(state.save_requested);
}

TEST_CASE("holing the last hole finishes the round and completes the course") {
    game_state state = single_hole_course();
    state.ball->strokes = 3;
    const shot_course course = current_shot_course(state);
    const glm::vec3 pin = course.hole.pin;
    play_shot(state, simulate_ball(resting_ball(state, pin), course, state.tuning));

    play_out(state, 0.016f);

    CHECK(round_finished(state.round));
    CHECK(*state.round.strokes[0] == 3);
    CHECK(state.save.completed_course_ids == std::vector<std::string>{"fixture_hub"});
    CHECK(state.save.holes_completed == 1);
    REQUIRE(state.ball.has_value());
    CHECK(state.ball->place == ball_place::in_cup);
    CHECK(near(state.ball->position, pin));
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
            shot_setup input;
            input.ball_start = from;
            input.aim_angle = static_cast<float>(i) * glm::quarter_pi<float>();
            input.club_id = "driver";
            input = stocked(input, state, 1.0f);
            const shot_result result = simulate_shot(input, current_shot_course(state), state.tuning, state.clubs);
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
    // Steep and fast it buries itself where it lands; slower it stays in the sand nearby.
    for (const float speed : {8.0f, 15.0f, 25.0f}) {
        ball_state ball;
        ball.radius = state.tuning.scale.ball_physics_radius_meters;
        ball.position = course.sand + glm::vec3(-0.5f, 0.5f, 0.0f);
        ball.velocity = glm::vec3(speed, -speed, 0.0f);
        const shot_result result = simulate_ball(ball, current_shot_course(state), state.tuning);
        CHECK(lie_at(state.area, result.rest_position) == ball_lie::bunker);
        const float sunk = terrain_height(state.area, result.rest_position) + ball.radius - result.rest_position.y;
        if (speed >= state.tuning.surfaces.bunker.dry.plug_speed) {
            CHECK(sunk > 0.5f * state.tuning.surfaces.bunker.dry.plug_depth);
            CHECK(horizontal_distance(result.rest_position, course.sand) < 0.2f);
        } else {
            CHECK(sunk < 0.001f);
            CHECK(horizontal_distance(result.rest_position, course.sand) < 2.0f);
        }
    }
}

TEST_CASE("a ball rolling into a tee box's side bounces off it, never up onto its top") {
    game_state state = started_hole();
    still_air(state);
    const tee_box& box = state.area.tee_boxes[state.ball->hole];
    const glm::vec3 across(box.down_hole.z, 0.0f, -box.down_hole.x);
    ball_state ball = resting_ball(state, box.center + across * (box.half_width + 1.0f));
    REQUIRE(!on_tee_box(box, ball.position));
    REQUIRE(box.center.y - ball.position.y > 2.0f * ball.radius);
    ball.velocity = -across * 4.0f;
    const shot_result result = simulate_ball(ball, current_shot_course(state), state.tuning);
    CHECK(!on_tee_box(box, result.rest_position));
    CHECK(glm::dot(result.rest_position - box.center, across) > box.half_width);
    float highest = 0.0f;
    for (const glm::vec3& point : result.trajectory) {
        highest = std::max(highest, point.y);
    }
    CHECK(highest < box.center.y);
}
