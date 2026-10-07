#pragma once

// Shared test helpers. Gameplay tests use the shipped tuning, clubs and
// rewards with the small hand-made courses in tests/fixtures, so content
// edits in assets/ do not break them.

#include "game/content_files.h"
#include "game/course_definition.h"
#include "game/course_loader.h"
#include "game/course_session.h"
#include "game/game_content.h"
#include "game/game_input.h"
#include "game/game_state.h"
#include "game/hole_data.h"
#include "game/play_area.h"
#include "game/round_state.h"
#include "game/text_assets.h"
#include "physics/terrain.h"
#include "physics/vector_math.h"

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

// The shipped strings, styles and font, loaded once per test run.
inline const text_assets& shipped_text_assets() {
    static const text_assets text = *load_text_assets(asset_root());
    return text;
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

// Picks the ball left in its cup after holing out (state.cup_ball) from
// beside the cup, then puts the player back where they stood, so the next
// hole can start from there.
inline void pick_up_from_cup(game_state& state) {
    const glm::vec3 standing = state.player.position;
    if (state.cup_ball) {
        state.player.position = *state.cup_ball;
        pick_up_cup_ball(state);
    }
    state.player.position = standing;
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

// A straight hole from `tee` to `pin` on a ribbon of the given width.
inline hole_data straight_hole(const glm::vec3& tee, const glm::vec3& pin, const float width) {
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
inline void play_hole(game_state& state, const hole_data& hole) {
    state.course_holes = {hole};
    state.round = start_round(1);
    state.area = build_hole_area(hole, state.tuning);
    state.hole = active_hole{0, hole.tee_position, hole.pin_position};
    mark_terrain_render_dirty(state);
}

// No wind, drag or spin, so ball motion depends only on contact.
inline void still_air(game_state& state) {
    state.tuning.wind = wind_tuning{};
    state.tuning.physics.drag_coeff = 0.0f;
    state.tuning.physics.magnus_coeff = 0.0f;
    state.tuning.physics.spin_decay = 0.0f;
}

inline float horizontal_speed(const glm::vec3& velocity) {
    return glm::length(horizontal(velocity));
}

inline glm::vec3 resting_on_terrain(const game_state& state, const glm::vec3& position) {
    const terrain_sample sample = sample_area(state.area, position);
    return sample.point + ground_normal(sample.normal) * state.ball.radius;
}

// An unturned ellipse zone; a circle when the radii are equal.
inline material_zone ellipse_zone(const material_zone_type type,
                                  const glm::vec3& center,
                                  const float radius_x,
                                  const float radius_z) {
    material_zone zone;
    zone.type = type;
    zone.center = center;
    zone.radii = glm::vec2(radius_x, radius_z);
    return zone;
}

inline material_zone circle_zone(const material_zone_type type, const glm::vec3& center, const float radius) {
    return ellipse_zone(type, center, radius, radius);
}

// A ribbon with no zones (bunker and water depths don't matter then).
inline terrain_mesh plain_terrain_mesh(const terrain_spline& spline) {
    return build_terrain_mesh(spline);
}

inline terrain_sample sample_spline(const terrain_spline& spline, const glm::vec3& position, const float fallback_y) {
    return sample_terrain_mesh(plain_terrain_mesh(spline), position, fallback_y);
}
