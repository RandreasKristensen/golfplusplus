#include "doctest.h"

#include "core/input.h"
#include "game/game_state.h"
#include "game/hole_loader.h"
#include "physics/terrain.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <optional>
#include <string>

namespace {
void enter_aiming(game_state& state) {
    input_state input;
    input.space.pressed = true;
    update_game(state, input, 0.016f);
}

void enter_addressing(game_state& state) {
    enter_aiming(state);

    input_state input;
    input.space.pressed = true;
    update_game(state, input, 0.016f);
}

void launch_selected_club(game_state& state) {
    enter_addressing(state);

    input_state input;
    input.space.pressed = true;
    update_game(state, input, 0.016f);

    input.reset_frame();
    input.space.pressed = true;
    update_game(state, input, 0.016f);
}

float horizontal_speed(const glm::vec3& velocity) {
    return std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
}

bool near_float(const float a, const float b, const float eps = 0.00001f) {
    return std::fabs(a - b) <= eps;
}

void rebuild_cached_terrain_mesh(game_tuning& tuning) {
    tuning.terrain_mesh_data = build_terrain_mesh(tuning.terrain);
}

glm::vec3 terrain_clamped_tee(const game_tuning& tuning) {
    glm::vec3 position = tuning.course.tee_position;
    position.y = sample_terrain_mesh(tuning.terrain_mesh_data, position, tuning.ground_y).point.y;
    return position;
}

std::string asset_root() {
#ifdef VCR_GOLF_ASSETS_DIR
    return VCR_GOLF_ASSETS_DIR;
#else
    return "assets";
#endif
}
}

TEST_CASE("walking movement changes player position and yaw") {
    game_state state = make_initial_game_state();
    const glm::vec3 start_position = state.player.position;
    const float start_yaw = state.player.yaw;

    input_state input;
    input.up.is_down = true;
    input.left.is_down = true;

    update_game(state, input, 0.25f);

    CHECK(glm::length(state.player.position - start_position) > 0.0f);
    CHECK(state.player.yaw > start_yaw);
    CHECK(state.mode == game_mode::walking);
}

TEST_CASE("space far from ball does not enter aiming") {
    game_state state = make_initial_game_state();
    state.player.position = glm::vec3(20.0f, 0.0f, 0.0f);

    input_state input;
    input.space.pressed = true;
    update_game(state, input, 0.016f);

    CHECK(state.mode == game_mode::walking);
}

TEST_CASE("space near ball enters aiming") {
    game_state state = make_initial_game_state();

    enter_aiming(state);

    CHECK(state.mode == game_mode::aiming);
    CHECK(state.swing.phase == swing_phase::idle);
    CHECK(state.stroke_count == 0);
    CHECK(glm::length(state.ball.velocity) == 0.0f);
}

TEST_CASE("space in aiming enters addressing without launching") {
    game_state state = make_initial_game_state();

    enter_addressing(state);

    CHECK(state.mode == game_mode::addressing);
    CHECK(state.swing.phase == swing_phase::idle);
    CHECK(state.stroke_count == 0);
    CHECK(glm::length(state.ball.velocity) == 0.0f);
}

TEST_CASE("locking aim moves player to left side address stance") {
    game_state state = make_initial_game_state();
    state.aim_angle = 0.0f;

    enter_addressing(state);

    CHECK(state.mode == game_mode::addressing);
    CHECK(state.player.position.x > state.ball.position.x);
    CHECK(state.player.position.y == sample_terrain_mesh(state.tuning.terrain_mesh_data,
                                                        state.player.position,
                                                        state.tuning.ground_y).point.y);
}

TEST_CASE("first address space press starts swing timing") {
    game_state state = make_initial_game_state();
    enter_addressing(state);

    input_state input;
    input.space.pressed = true;
    input.space.is_down = true;

    update_game(state, input, 0.016f);

    CHECK(state.mode == game_mode::addressing);
    CHECK(state.swing.phase == swing_phase::timing);
    CHECK(state.stroke_count == 0);
    CHECK(glm::length(state.ball.velocity) == 0.0f);
}

TEST_CASE("course initializes tee and pin") {
    game_state state = make_initial_game_state();
    const std::optional<hole_data> hole = load_hole_from_file(asset_root() + "/holes/test.json");

    if (hole) {
        CHECK(state.tuning.course.tee_position == hole->tee_position);
        CHECK(state.tuning.course.pin_position == hole->pin_position);
    } else {
        CHECK(state.tuning.course.tee_position == glm::vec3(0.0f, 0.0f, 0.0f));
        CHECK(state.tuning.course.pin_position == glm::vec3(0.0f, 0.0f, 80.0f));
    }
    CHECK(state.tuning.course.cup_radius > 0.0f);
    CHECK(state.tuning.course.extent > state.tuning.course.pin_position.z);
    CHECK(state.ball.position.y == sample_terrain_mesh(state.tuning.terrain_mesh_data,
                                                       state.tuning.course.tee_position,
                                                       state.tuning.ground_y).point.y);
    CHECK(state.ball.position.x == state.tuning.course.tee_position.x);
    CHECK(state.ball.position.z == state.tuning.course.tee_position.z);
    CHECK(state.tuning.clubs.size() == 3);
    CHECK(state.tuning.clubs[0].id == "putter");
    CHECK(state.tuning.clubs[1].id == "pitching_wedge");
    CHECK(state.tuning.clubs[2].id == "seven_iron");
    CHECK(state.tuning.clubs[0].label == "P");
    CHECK(state.tuning.clubs[1].label == "PW");
    CHECK(state.tuning.clubs[2].label == "7I");
}

TEST_CASE("default tuning uses one-meter world scale") {
    const game_tuning tuning = default_game_tuning();

    CHECK(near_float(tuning.scale.meters_per_world_unit, 1.0f));
    CHECK(near_float(tuning.scale.ball_physics_radius_meters, 0.021335f));
    CHECK(near_float(tuning.scale.ball_mass_kg, 0.04593f));
    CHECK(near_float(tuning.scale.cup_physics_radius_meters, 0.053975f));
    CHECK(near_float(tuning.course.cup_radius, tuning.scale.cup_physics_radius_meters));
}

TEST_CASE("default tuning separates physical and visual golf object sizes") {
    const game_tuning tuning = default_game_tuning();

    CHECK(near_float(tuning.scale.ball_visual_radius_meters, 0.10f));
    CHECK(tuning.scale.ball_visual_radius_meters > tuning.scale.ball_physics_radius_meters);
    CHECK(tuning.scale.ball_visual_radius_meters < 0.35f);
    CHECK(near_float(tuning.scale.cup_visual_radius_meters, 0.10f));
    CHECK(tuning.scale.cup_visual_radius_meters > tuning.scale.cup_physics_radius_meters);
    CHECK(near_float(tuning.scale.pin_visual_height_meters, 2.10f));
}

TEST_CASE("hole editor json loads into game tuning course data") {
    const std::optional<hole_data> hole = load_hole_from_file(asset_root() + "/holes/test.json");
    CHECK(hole.has_value());
    if (!hole) {
        return;
    }

    game_tuning tuning = default_game_tuning();

    CHECK(tuning.course.id == hole->id);
    CHECK(tuning.course.name == hole->name);
    CHECK(tuning.course.par == hole->par);
    CHECK(tuning.course.tee_position == hole->tee_position);
    CHECK(tuning.course.pin_position == hole->pin_position);
    CHECK(tuning.wind_seed == hole->wind_seed);
    CHECK(tuning.course.spline.width == hole->spline.width);
    CHECK(tuning.course.spline.control_points.size() == hole->spline.control_points.size());
    CHECK(tuning.terrain_mesh_data.vertices.size() > 0);
    CHECK(tuning.terrain_mesh_data.indices.size() > 0);
    CHECK(tuning.course.material_zones.size() == hole->material_zones.size());
    CHECK(tuning.course.extent >= estimate_course_extent(*hole));
}

TEST_CASE("fallback hole populates cached terrain mesh") {
    game_tuning tuning;
    tuning.course.tee_position = glm::vec3(0.0f, 2.0f, 0.0f);
    tuning.course.pin_position = glm::vec3(0.0f, 2.0f, 80.0f);
    tuning.ground_y = 2.0f;
    tuning.terrain.control_points = {
        glm::vec3(0.0f, 2.0f, 0.0f),
        glm::vec3(0.0f, 2.0f, 40.0f),
        glm::vec3(0.0f, 2.0f, 80.0f)
    };
    tuning.terrain.width = 18.0f;
    tuning.terrain.sample_count = 32;
    rebuild_cached_terrain_mesh(tuning);

    CHECK(tuning.terrain_mesh_data.vertices.size() > 0);
    CHECK(tuning.terrain_mesh_data.indices.size() > 0);
    CHECK(sample_terrain_mesh(tuning.terrain_mesh_data, glm::vec3(0.0f, 0.0f, 20.0f), -5.0f).point.y == 2.0f);
}

TEST_CASE("space-space in addressing launches selected club shot and enters follow") {
    game_state state = make_initial_game_state();
    enter_addressing(state);

    input_state input;
    input.space.pressed = true;
    update_game(state, input, 0.016f);

    input.reset_frame();
    update_game(state, input, 0.35f);

    input.reset_frame();
    input.space.pressed = true;
    input.space.is_down = true;
    update_game(state, input, 0.016f);

    CHECK(state.swing.phase == swing_phase::idle);
    CHECK(state.mode == game_mode::following_shot);
    CHECK(state.stroke_count == 1);
    CHECK(glm::length(state.ball.velocity) > 0.0f);
    CHECK(glm::length(state.shot_camera_position) > 0.0f);
}

TEST_CASE("launch tuning changes shot speed") {
    game_state low_power = make_initial_game_state();
    game_state high_power = make_initial_game_state();
    low_power.tuning.min_swing_power = 0.1f;
    high_power.tuning.min_swing_power = 0.8f;

    launch_selected_club(low_power);
    launch_selected_club(high_power);

    CHECK(glm::length(high_power.ball.velocity) > glm::length(low_power.ball.velocity));
}

TEST_CASE("club loft changes launch angle") {
    game_state putter = make_initial_game_state();
    game_state wedge = make_initial_game_state();
    putter.tuning.min_swing_power = 1.0f;
    wedge.tuning.min_swing_power = 1.0f;
    wedge.selected_club = 1;

    launch_selected_club(putter);
    launch_selected_club(wedge);

    const float putter_rise = putter.ball.velocity.y / horizontal_speed(putter.ball.velocity);
    const float wedge_rise = wedge.ball.velocity.y / horizontal_speed(wedge.ball.velocity);
    CHECK(wedge.tuning.clubs[wedge.selected_club].stats.loft_degrees > putter.tuning.clubs[putter.selected_club].stats.loft_degrees);
    CHECK(wedge_rise > putter_rise);
}

TEST_CASE("stop speed tuning controls moving threshold") {
    ball_state ball;
    ball.velocity = glm::vec3(1.0f, 0.0f, 0.0f);

    game_tuning loose = default_game_tuning();
    loose.stop_speed = 2.0f;
    CHECK(!ball_is_moving(ball, loose));

    game_tuning strict = default_game_tuning();
    strict.stop_speed = 0.5f;
    CHECK(ball_is_moving(ball, strict));
}

TEST_CASE("moving threshold uses spline terrain height") {
    ball_state ball;
    ball.velocity = glm::vec3(0.0f);
    ball.position = glm::vec3(0.0f, 3.0f, 5.0f);

    game_tuning tuning = default_game_tuning();
    tuning.ground_y = 0.0f;
    tuning.terrain.control_points = {
        glm::vec3(0.0f, 3.0f, 0.0f),
        glm::vec3(0.0f, 3.0f, 10.0f)
    };
    tuning.terrain.width = 8.0f;
    tuning.terrain.sample_count = 16;
    rebuild_cached_terrain_mesh(tuning);

    CHECK(!ball_is_moving(ball, tuning));

    ball.position.y = 3.5f;
    CHECK(ball_is_moving(ball, tuning));
}

TEST_CASE("walking height uses cached terrain mesh instead of flat ground") {
    game_state state = make_initial_game_state();
    state.tuning.ground_y = 0.0f;
    state.tuning.terrain.control_points = {
        glm::vec3(0.0f, 3.0f, 0.0f),
        glm::vec3(0.0f, 3.0f, 20.0f)
    };
    state.tuning.terrain.width = 8.0f;
    state.tuning.terrain.sample_count = 16;
    rebuild_cached_terrain_mesh(state.tuning);
    state.player.position = glm::vec3(0.0f, 0.0f, 5.0f);

    input_state input;
    input.up.is_down = true;
    update_game(state, input, 0.016f);

    CHECK(state.player.position.y == 3.0f);
}

TEST_CASE("game update collides ball against spline terrain height") {
    game_state state = make_initial_game_state();
    state.mode = game_mode::following_shot;
    state.tuning.physics.drag_coeff = 0.0f;
    state.tuning.physics.magnus_coeff = 0.0f;
    state.tuning.wind.base_speed = 0.0f;
    state.tuning.wind.speed_variation = 0.0f;
    state.tuning.ground_restitution = 0.0f;
    state.tuning.ground_friction = 0.0f;
    state.tuning.ground_roll_friction = 0.0f;
    state.tuning.ground_settle_speed = 10.0f;
    state.tuning.terrain.control_points = {
        glm::vec3(0.0f, 4.0f, 0.0f),
        glm::vec3(0.0f, 4.0f, 10.0f)
    };
    state.tuning.terrain.width = 8.0f;
    state.tuning.terrain.sample_count = 16;
    rebuild_cached_terrain_mesh(state.tuning);
    state.ball.position = glm::vec3(0.0f, 4.05f, 5.0f);
    state.ball.velocity = glm::vec3(0.0f, -2.0f, 0.0f);

    input_state input;
    update_game(state, input, 0.1f);

    CHECK(state.ball.position.y == 4.0f);
    CHECK(state.ball.velocity.y == 0.0f);
}

TEST_CASE("roll friction preserves slope tangent velocity") {
    game_state state = make_initial_game_state();
    state.mode = game_mode::following_shot;
    state.tuning.ground_roll_friction = 0.0f;
    state.tuning.ground_settle_speed = 10.0f;
    state.tuning.stop_speed = 0.01f;
    state.tuning.terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 5.0f, 10.0f)
    };
    state.tuning.terrain.width = 8.0f;
    state.tuning.terrain.sample_count = 16;
    rebuild_cached_terrain_mesh(state.tuning);

    const terrain_sample terrain = sample_terrain_mesh(state.tuning.terrain_mesh_data, glm::vec3(0.0f, 0.0f, 5.0f), 0.0f);
    const glm::vec3 tangent = glm::normalize(glm::vec3(0.0f, 5.0f, 10.0f));
    state.ball.position = terrain.point;
    state.ball.velocity = tangent;

    input_state input;
    update_game(state, input, 0.0f);

    CHECK(state.ball.velocity.y > 0.1f);
    CHECK(glm::length(state.ball.velocity - tangent) < 0.001f);
}

TEST_CASE("ground roll friction stops a grounded ball") {
    game_state state = make_initial_game_state();
    state.tuning.physics.drag_coeff = 0.0f;
    state.tuning.wind.base_speed = 0.0f;
    state.tuning.wind.speed_variation = 0.0f;
    state.tuning.ground_restitution = 0.0f;
    state.tuning.ground_roll_friction = 10.0f;
    state.tuning.ground_settle_speed = 10.0f;
    state.tuning.stop_speed = 0.05f;
    state.ball.position = state.tuning.course.tee_position;
    state.ball.velocity = glm::vec3(1.0f, 0.0f, 0.0f);

    input_state input;
    for (int i = 0; i < 8; ++i) {
        update_game(state, input, 0.05f);
    }

    CHECK(horizontal_speed(state.ball.velocity) == 0.0f);
    CHECK(!ball_is_moving(state.ball, state.tuning));
}

TEST_CASE("retee resets ball and cancels swing without adding a stroke") {
    game_state state = make_initial_game_state();

    enter_addressing(state);

    input_state input;
    input.space.pressed = true;
    update_game(state, input, 0.016f);

    state.ball.position = glm::vec3(5.0f, 1.0f, 6.0f);
    state.ball.velocity = glm::vec3(2.0f, 3.0f, 4.0f);
    state.ball.spin = glm::vec3(1.0f, 0.0f, 0.0f);

    input.reset_frame();
    input.retee.pressed = true;
    update_game(state, input, 0.016f);

    CHECK(state.ball.position == terrain_clamped_tee(state.tuning));
    CHECK(glm::length(state.ball.velocity) == 0.0f);
    CHECK(glm::length(state.ball.spin) == 0.0f);
    CHECK(state.swing.phase == swing_phase::idle);
    CHECK(state.mode == game_mode::walking);
    CHECK(state.stroke_count == 0);
}

TEST_CASE("retee works while ball is moving") {
    game_state state = make_initial_game_state();
    state.ball.position = glm::vec3(12.0f, 2.0f, 30.0f);
    state.ball.velocity = glm::vec3(5.0f, 4.0f, 3.0f);
    state.stroke_count = 1;

    input_state input;
    input.retee.pressed = true;
    update_game(state, input, 0.016f);

    CHECK(state.ball.position == terrain_clamped_tee(state.tuning));
    CHECK(glm::length(state.ball.velocity) == 0.0f);
    CHECK(state.stroke_count == 1);
}

TEST_CASE("club selection wraps on button presses") {
    game_state state = make_initial_game_state();
    enter_aiming(state);

    input_state input;
    input.up.pressed = true;
    update_game(state, input, 0.016f);
    CHECK(state.selected_club == state.tuning.clubs.size() - 1);

    input.reset_frame();
    input.down.pressed = true;
    update_game(state, input, 0.016f);
    CHECK(state.selected_club == 0);
}

TEST_CASE("held left and right adjust aim") {
    game_state state = make_initial_game_state();
    enter_aiming(state);

    input_state input;
    input.left.is_down = true;
    update_game(state, input, 0.25f);
    CHECK(state.aim_angle > 0.0f);

    input.left.is_down = false;
    input.right.is_down = true;
    update_game(state, input, 0.05f);
    update_game(state, input, 0.05f);
    CHECK(state.aim_angle < 0.0f);
}

TEST_CASE("launched ball advances through physics updates") {
    game_state state = make_initial_game_state();

    launch_selected_club(state);

    const glm::vec3 launch_position = state.ball.position;
    input_state input;
    input.reset_frame();
    update_game(state, input, 0.016f);

    CHECK(glm::length(state.ball.position - launch_position) > 0.0f);
}

TEST_CASE("follow mode returns to walking when ball is stopped") {
    game_state state = make_initial_game_state();
    const glm::vec3 player_position = state.player.position;
    state.mode = game_mode::following_shot;
    state.ball.velocity = glm::vec3(0.0f);
    state.ball.spin = glm::vec3(0.0f);
    state.ball.position = glm::vec3(8.0f, 0.0f, 10.0f);

    input_state input;
    update_game(state, input, 0.016f);

    CHECK(state.mode == game_mode::walking);
    CHECK(state.player.position == player_position);
    CHECK(!can_interact_with_ball(state));
}
