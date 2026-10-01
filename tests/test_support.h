#pragma once

// Shared test helpers. Gameplay tests use the shipped tuning, clubs and
// rewards with the small hand-made courses in tests/fixtures, so content
// edits in assets/ do not break them.

#include "game/course_definition.h"
#include "game/course_loader.h"
#include "game/course_session.h"
#include "game/game_content.h"
#include "game/game_input.h"
#include "game/game_state.h"
#include "physics/terrain.h"

#include <cmath>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

inline std::string asset_root() {
    return GOLFPP_ASSETS_DIR;
}

inline std::string fixture_root() {
    return GOLFPP_TEST_FIXTURES_DIR;
}

inline std::string fixture_hole_path(const std::string& name) {
    return fixture_root() + "/holes/" + name + ".json";
}

inline bool near(const float a, const float b, const float epsilon = 0.00001f) {
    return std::abs(a - b) <= epsilon;
}

inline bool near(const glm::vec3& a, const glm::vec3& b, const float epsilon = 0.00001f) {
    return glm::length(a - b) <= epsilon;
}

// The shipped content (assets/), loaded once per test run.
inline const game_content& shipped_content() {
    static const game_content content = *load_game_content(asset_root()).content;
    return content;
}

// Shipped tuning, clubs and rewards; courses and holes from tests/fixtures.
inline game_content fixture_content() {
    game_content content = shipped_content();
    content.asset_root = fixture_root();
    content.courses = load_courses_from_directory(fixture_root() + "/courses");
    return content;
}

inline course_definition fixture_course(const std::vector<std::string>& holes) {
    course_definition course;
    course.id = "fixture_course";
    course.name = "Fixture Course";
    course.holes = holes;
    return course;
}

inline course_definition fixture_hub_course() {
    return *load_course_from_file(fixture_root() + "/courses/hub.json");
}

// A fresh state playing `course` (fixture holes).
inline game_state started_game(const course_definition& course) {
    game_state state = make_game_state(fixture_content(), save_data{});
    start_course(state, course);
    return state;
}

// A fresh state on hole 1 of a two-hole fixture course.
inline game_state started_hole() {
    return started_game(fixture_course({"test", "test2"}));
}

inline game_input action_input() {
    game_input input;
    input.action = true;
    return input;
}

inline void enter_aiming(game_state& state) {
    update_game(state, action_input(), 0.016f);
}

inline void enter_addressing(game_state& state) {
    enter_aiming(state);
    update_game(state, action_input(), 0.016f);
}

// Starts the swing meter and hits on the next frame.
inline void hit_selected_club(game_state& state) {
    enter_addressing(state);
    update_game(state, action_input(), 0.016f);
    update_game(state, action_input(), 0.016f);
}

// A ribbon with no zones (bunker and water depths don't matter then).
inline terrain_mesh plain_terrain_mesh(const terrain_spline& spline) {
    return build_terrain_mesh(spline, {}, terrain_zone_tuning{});
}

inline terrain_sample sample_spline(const terrain_spline& spline, const glm::vec3& position, const float fallback_y) {
    return sample_terrain_mesh(plain_terrain_mesh(spline), position, fallback_y);
}
