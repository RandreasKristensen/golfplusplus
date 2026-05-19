#include "doctest.h"

#include "core/input.h"
#include "game/course_loader.h"
#include "game/game_state.h"
#include "game/game_content.h"
#include "game/hole_loader.h"
#include "game/round_state.h"
#include "game/save_data.h"
#include "physics/terrain.h"
#include "quest/quest_engine.h"
#include "quest/quest_loader.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <optional>
#include <string>
#include <vector>

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
    tuning.terrain_mesh_data = build_terrain_mesh(tuning.terrain, tuning.course.material_zones, tuning.zone_tuning);
}

glm::vec3 terrain_clamped_tee(const game_tuning& tuning) {
    const terrain_sample sample = sample_terrain_mesh(tuning.terrain_mesh_data, tuning.course.tee_position, tuning.ground_y);
    return sample.point + sample.normal * tuning.scale.ball_physics_radius_meters;
}

std::string asset_root() {
#ifdef VCR_GOLF_ASSETS_DIR
    return VCR_GOLF_ASSETS_DIR;
#else
    return "assets";
#endif
}

std::vector<std::string> expected_club_ids() {
    return {
        "putter",
        "sand_wedge",
        "pitching_wedge",
        "nine_iron",
        "seven_iron",
        "five_iron",
        "seven_wood",
        "four_wood",
        "driver"
    };
}

std::vector<std::string> expected_club_labels() {
    return {"P", "SW", "PW", "9I", "7I", "5I", "7W", "4W", "D"};
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
    CHECK(state.ball.radius == state.tuning.scale.ball_physics_radius_meters);
    CHECK(state.ball.mass == state.tuning.scale.ball_mass_kg);
    CHECK(state.ball.position == terrain_clamped_tee(state.tuning));
    const std::vector<std::string> club_ids = expected_club_ids();
    const std::vector<std::string> club_labels = expected_club_labels();
    CHECK(state.tuning.clubs.size() == club_ids.size());
    for (std::size_t i = 0; i < club_ids.size() && i < state.tuning.clubs.size(); ++i) {
        CHECK(state.tuning.clubs[i].id == club_ids[i]);
        CHECK(state.tuning.clubs[i].label == club_labels[i]);
        CHECK(state.tuning.clubs[i].bag_order == static_cast<int>(i));
    }
}

TEST_CASE("fallback clubs match full bag order") {
    const std::vector<club_definition> clubs = fallback_club_definitions();
    const std::vector<std::string> club_ids = expected_club_ids();
    const std::vector<std::string> club_labels = expected_club_labels();

    CHECK(clubs.size() == club_ids.size());
    for (std::size_t i = 0; i < club_ids.size() && i < clubs.size(); ++i) {
        CHECK(clubs[i].id == club_ids[i]);
        CHECK(clubs[i].label == club_labels[i]);
        CHECK(clubs[i].bag_order == static_cast<int>(i));
    }
}

TEST_CASE("club powers target a longer distance ladder") {
    const game_tuning tuning = default_game_tuning();

    CHECK(tuning.clubs.size() == expected_club_ids().size());
    if (tuning.clubs.size() != expected_club_ids().size()) {
        return;
    }
    CHECK(near_float(tuning.physics.drag_coeff, 0.004f));
    CHECK(near_float(tuning.clubs[1].stats.power, 25.0f));
    CHECK(near_float(tuning.clubs[2].stats.power, 30.5f));
    CHECK(near_float(tuning.clubs[3].stats.power, 33.5f));
    CHECK(near_float(tuning.clubs[4].stats.power, 38.5f));
    CHECK(near_float(tuning.clubs[5].stats.power, 45.0f));
    CHECK(near_float(tuning.clubs[6].stats.power, 53.0f));
    CHECK(near_float(tuning.clubs[7].stats.power, 65.0f));
    CHECK(near_float(tuning.clubs[8].stats.power, 80.0f));

    for (std::size_t i = 2; i + 1 < tuning.clubs.size(); ++i) {
        CHECK(tuning.clubs[i + 1].stats.power > tuning.clubs[i].stats.power);
    }
}

TEST_CASE("default tuning uses one-meter world scale") {
    const game_tuning tuning = default_game_tuning();

    CHECK(near_float(tuning.scale.meters_per_world_unit, 1.0f));
    CHECK(near_float(tuning.scale.ball_physics_radius_meters, 0.021335f));
    CHECK(near_float(tuning.scale.ball_mass_kg, 0.04593f));
    CHECK(near_float(tuning.scale.cup_physics_radius_meters, 0.65f));
    CHECK(near_float(tuning.course.cup_radius, tuning.scale.cup_physics_radius_meters));
}

TEST_CASE("default tuning separates physical and visual golf object sizes") {
    const game_tuning tuning = default_game_tuning();

    CHECK(near_float(tuning.scale.ball_visual_radius_meters, 0.10f));
    CHECK(tuning.scale.ball_visual_radius_meters > tuning.scale.ball_physics_radius_meters);
    CHECK(tuning.scale.ball_visual_radius_meters < 0.35f);
    CHECK(near_float(tuning.scale.cup_visual_radius_meters, 0.75f));
    CHECK(tuning.scale.cup_visual_radius_meters > tuning.scale.cup_physics_radius_meters);
    CHECK(near_float(tuning.scale.pin_visual_height_meters, 2.10f));
}

TEST_CASE("default tuning has arcade-sized cup") {
    const game_tuning tuning = default_game_tuning();

    CHECK(tuning.scale.cup_physics_radius_meters > 0.5f);
    CHECK(tuning.scale.cup_visual_radius_meters > tuning.scale.cup_physics_radius_meters);
    CHECK(tuning.course.cup_radius > tuning.scale.ball_visual_radius_meters * 5.0f);
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

TEST_CASE("content layer loads courses clubs and quests from asset root") {
    const game_content content = load_game_content(asset_root());

    CHECK(!content.clubs.empty());
    CHECK(!content.courses.empty());
    CHECK(!content.quests.empty());

    bool found_dev = false;
    bool found_quest = false;
    for (const course_definition& course : content.courses) {
        if (course.id == "dev_course") {
            found_dev = true;
            CHECK(course.hole_count == 1);
            CHECK(course.holes.size() == 1);
        }
    }
    for (const quest_definition& quest : content.quests) {
        if (quest.id == "starter_cash") {
            found_quest = true;
            CHECK(quest.reward.money == 25);
        }
    }

    CHECK(found_dev);
    CHECK(found_quest);
}

TEST_CASE("course manifests load nine and eighteen hole definitions in order") {
    const std::optional<course_definition> nine = load_course_from_file(asset_root() + "/courses/test_nine.json");
    const std::optional<course_definition> eighteen = load_course_from_file(asset_root() + "/courses/test_eighteen.json");

    CHECK(nine.has_value());
    CHECK(eighteen.has_value());
    if (!nine || !eighteen) {
        return;
    }

    CHECK(nine->id == "test_nine");
    CHECK(nine->hole_count == 9);
    CHECK(nine->holes.size() == 9);
    CHECK(nine->holes.front() == "holes/test.json");
    CHECK(nine->holes.back() == "holes/test.json");

    CHECK(eighteen->id == "test_eighteen");
    CHECK(eighteen->hole_count == 18);
    CHECK(eighteen->holes.size() == 18);
}

TEST_CASE("missing course hole path fails cleanly") {
    game_tuning tuning = default_game_tuning();
    course_definition course;
    course.id = "bad_course";
    course.name = "Bad Course";
    course.hole_count = 1;
    course.holes = {"holes/missing.json"};

    CHECK(!load_hole_runtime(tuning, course, 0, asset_root()));
    CHECK(!load_hole_runtime(tuning, course, 1, asset_root()));
}

TEST_CASE("round state records strokes and finishes after course length") {
    course_definition course;
    course.id = "two_hole";
    course.name = "Two Hole";
    course.hole_count = 2;
    course.holes = {"holes/test.json", "holes/test.json"};

    round_state round = start_course(course);
    CHECK(round.current_hole_index == 0);
    CHECK(round.strokes_per_hole.size() == 2);
    CHECK(!round.finished);

    CHECK(complete_hole(round, 4));
    CHECK(round.current_hole_index == 1);
    CHECK(round.strokes_per_hole[0] == 4);

    CHECK(!complete_hole(round, 5));
    CHECK(round.finished);
    CHECK(round.strokes_per_hole[1] == 5);
}

TEST_CASE("game course completion preserves score and resets transient hole state") {
    course_definition course;
    course.id = "two_hole";
    course.name = "Two Hole";
    course.hole_count = 2;
    course.holes = {"holes/test.json", "holes/test.json"};

    game_state state = make_initial_game_state(asset_root());
    CHECK(start_game_course(state, course));
    if (state.active_course.id != course.id) {
        return;
    }
    state.stroke_count = 3;
    state.ball.velocity = glm::vec3(4.0f, 0.0f, 0.0f);
    state.swing.phase = swing_phase::timing;

    CHECK(complete_current_hole(state));
    CHECK(state.round.current_hole_index == 1);
    CHECK(state.round.strokes_per_hole[0] == 3);
    CHECK(state.save.current_course_id == "two_hole");
    CHECK(state.save.current_hole_index == 1);
    CHECK(state.save.hole_scores.at(0) == 3);
    CHECK(state.stroke_count == 0);
    CHECK(glm::length(state.ball.velocity) == 0.0f);
    CHECK(state.swing.phase == swing_phase::idle);
    CHECK(state.mode == game_mode::walking);
}

TEST_CASE("save data round trips progress and migrates missing version") {
    save_data save;
    save.version = current_save_version;
    save.money = 70;
    save.unlocked_items = {"driver"};
    save.completed_quest_ids = {"starter_cash"};
    save.current_course_id = "test_nine";
    save.current_hole_index = 3;
    save.hole_scores[0] = 4;
    save.hole_scores[1] = 5;

    const std::optional<save_data> parsed = parse_save_data(save_data_to_json(save));
    CHECK(parsed.has_value());
    if (!parsed) {
        return;
    }
    CHECK(parsed->version == current_save_version);
    CHECK(parsed->money == 70);
    CHECK(parsed->unlocked_items.size() == 1);
    CHECK(parsed->completed_quest_ids.size() == 1);
    CHECK(parsed->current_course_id == "test_nine");
    CHECK(parsed->current_hole_index == 3);
    CHECK(parsed->hole_scores.at(0) == 4);
    CHECK(parsed->hole_scores.at(1) == 5);

    const std::optional<save_data> migrated = parse_save_data("{\"money\":5,\"current_hole_index\":-2}");
    CHECK(migrated.has_value());
    if (!migrated) {
        return;
    }
    CHECK(migrated->version == current_save_version);
    CHECK(migrated->money == 5);
    CHECK(migrated->current_hole_index == 0);
}

TEST_CASE("quest parser engine and reward application are deterministic") {
    const std::optional<quest_definition> quest = load_quest_from_file(asset_root() + "/quests/starter_cash.json");
    CHECK(quest.has_value());
    if (!quest) {
        return;
    }
    CHECK(quest->steps.size() == 2);
    CHECK(quest->reward.money == 25);

    quest_session session = start_quest(*quest);
    const quest_step* first = current_quest_step(session);
    CHECK(first != nullptr);
    if (first == nullptr) {
        return;
    }
    CHECK(first->id == "intro");

    quest_outcome outcome = advance_quest(session, 0);
    CHECK(!outcome.completed);
    const quest_step* second = current_quest_step(session);
    CHECK(second != nullptr);
    if (second == nullptr) {
        return;
    }
    CHECK(second->id == "accepted");

    outcome = advance_quest(session, 0);
    CHECK(outcome.completed);
    CHECK(outcome.money == 25);

    save_data save;
    CHECK(apply_quest_completion_once(save, session, outcome));
    CHECK(save.money == 25);
    CHECK(save.completed_quest_ids.size() == 1);
    CHECK(!apply_quest_completion_once(save, session, outcome));
    CHECK(save.money == 25);
    CHECK(save.completed_quest_ids.size() == 1);
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

TEST_CASE("putter swing meter advances at quarter speed") {
    game_state putter = make_initial_game_state();
    game_state wedge = make_initial_game_state();
    wedge.selected_club = 1;

    enter_addressing(putter);
    enter_addressing(wedge);

    input_state input;
    input.space.pressed = true;
    update_game(putter, input, 0.016f);
    update_game(wedge, input, 0.016f);

    input.reset_frame();
    update_game(putter, input, 0.35f);
    update_game(wedge, input, 0.35f);

    CHECK(putter.tuning.clubs[putter.selected_club].stats.timing_speed == 0.25f);
    CHECK(wedge.tuning.clubs[wedge.selected_club].stats.timing_speed == 1.0f);
    CHECK(putter.swing.power < wedge.swing.power * 0.25f);
}

TEST_CASE("putter rolls slower but loses less speed on grass") {
    game_state putt = make_initial_game_state();
    game_state wedge_roll = make_initial_game_state();
    wedge_roll.selected_club = 4;

    putt.mode = game_mode::following_shot;
    wedge_roll.mode = game_mode::following_shot;
    putt.tuning.physics.drag_coeff = 0.0f;
    wedge_roll.tuning.physics.drag_coeff = 0.0f;
    putt.tuning.wind.base_speed = 0.0f;
    wedge_roll.tuning.wind.base_speed = 0.0f;
    putt.tuning.wind.speed_variation = 0.0f;
    wedge_roll.tuning.wind.speed_variation = 0.0f;
    putt.tuning.ground_restitution = 0.0f;
    wedge_roll.tuning.ground_restitution = 0.0f;
    putt.tuning.ground_friction = 0.08f;
    wedge_roll.tuning.ground_friction = 0.08f;
    putt.tuning.ground_roll_friction = 1.0f;
    wedge_roll.tuning.ground_roll_friction = 1.0f;
    putt.tuning.ground_settle_speed = 10.0f;
    wedge_roll.tuning.ground_settle_speed = 10.0f;

    putt.ball.position = terrain_clamped_tee(putt.tuning);
    wedge_roll.ball.position = terrain_clamped_tee(wedge_roll.tuning);
    putt.ball.velocity = glm::vec3(1.0f, 0.0f, 0.0f);
    wedge_roll.ball.velocity = glm::vec3(1.0f, 0.0f, 0.0f);

    input_state input;
    update_game(putt, input, 0.05f);
    update_game(wedge_roll, input, 0.05f);

    CHECK(putt.tuning.clubs[putt.selected_club].stats.power < wedge_roll.tuning.clubs[wedge_roll.selected_club].stats.power);
    CHECK(putt.tuning.clubs[putt.selected_club].stats.roll_friction_scale < 1.0f);
    CHECK(horizontal_speed(putt.ball.velocity) > horizontal_speed(wedge_roll.ball.velocity));
}

TEST_CASE("stop speed tuning controls moving threshold") {
    game_tuning loose = default_game_tuning();
    const terrain_sample terrain = sample_terrain_mesh(loose.terrain_mesh_data,
                                                        glm::vec3(0.0f, 0.0f, 0.0f),
                                                        loose.ground_y);
    ball_state ball;
    ball.position = terrain.point + terrain.normal * ball.radius;
    ball.velocity = glm::vec3(1.0f, 0.0f, 0.0f);

    loose.stop_speed = 2.0f;
    CHECK(!ball_is_moving(ball, loose));

    game_tuning strict = default_game_tuning();
    ball.position = terrain.point + terrain.normal * ball.radius;
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

    ball.position.y = 3.0f + ball.radius + 0.01f;
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

    const terrain_sample sample = sample_terrain_mesh(state.tuning.terrain_mesh_data,
                                                      glm::vec3(0.0f, 0.0f, 5.0f),
                                                      state.tuning.ground_y);
    CHECK(near_float(glm::dot(state.ball.position - sample.point, sample.normal), state.ball.radius));
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
    state.ball.position = terrain.point + terrain.normal * state.ball.radius;
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
    state.ball.position = terrain_clamped_tee(state.tuning);
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

TEST_CASE("rangefinder formats rounded horizontal meters") {
    const glm::vec3 player_position(1.0f, 12.0f, 2.0f);
    const glm::vec3 pin_anchor(4.0f, -3.0f, 6.0f);

    CHECK(near_float(compute_rangefinder_distance_meters(player_position, pin_anchor, 1.0f), 5.0f));
    CHECK(near_float(compute_rangefinder_distance_meters(player_position, pin_anchor, 2.0f), 10.0f));
    CHECK(format_rangefinder_distance(124.49f) == "124M");
    CHECK(format_rangefinder_distance(124.50f) == "125M");
}

TEST_CASE("rangefinder is only active while walking with shift held") {
    game_state state = make_initial_game_state();

    input_state input;
    input.shift.is_down = true;
    update_game(state, input, 0.016f);

    CHECK(state.mode == game_mode::walking);
    CHECK(state.rangefinder_active);
    CHECK(state.rangefinder_distance_meters > 0.0f);
    CHECK(!state.rangefinder_distance_label.empty());

    input.reset_frame();
    input.shift.is_down = false;
    update_game(state, input, 0.016f);
    CHECK(!state.rangefinder_active);

    input.shift.is_down = true;
    update_game(state, input, 0.016f);
    CHECK(state.rangefinder_active);

    enter_aiming(state);
    input.reset_frame();
    input.shift.is_down = true;
    update_game(state, input, 0.016f);
    CHECK(state.mode == game_mode::aiming);
    CHECK(!state.rangefinder_active);

    input.reset_frame();
    input.shift.is_down = true;
    update_game(state, input, 0.016f);
    CHECK(!rangefinder_should_show(game_mode::addressing, input));
    CHECK(!rangefinder_should_show(game_mode::following_shot, input));
}

TEST_CASE("rangefinder distance changes as player walks toward pin") {
    game_state state = make_initial_game_state();

    input_state input;
    input.shift.is_down = true;
    update_game(state, input, 0.016f);
    const float starting_distance = state.rangefinder_distance_meters;

    input.up.is_down = true;
    update_game(state, input, 0.5f);

    CHECK(state.rangefinder_active);
    CHECK(state.rangefinder_distance_meters < starting_distance);
}

TEST_CASE("paper course map is only active while walking with enter held") {
    game_state state = make_initial_game_state();

    input_state input;
    input.enter.is_down = true;
    update_game(state, input, 0.016f);

    CHECK(state.mode == game_mode::walking);
    CHECK(state.course_map_active);

    input.reset_frame();
    input.enter.is_down = false;
    update_game(state, input, 0.016f);
    CHECK(!state.course_map_active);

    input.enter.is_down = true;
    update_game(state, input, 0.016f);
    CHECK(course_map_should_show(state.mode, input));

    enter_aiming(state);
    input.reset_frame();
    input.enter.is_down = true;
    update_game(state, input, 0.016f);

    CHECK(state.mode == game_mode::aiming);
    CHECK(!state.course_map_active);
}

TEST_CASE("escape or backspace returns shot setup to walking") {
    game_state aiming = make_initial_game_state();
    enter_aiming(aiming);

    input_state input;
    input.escape.pressed = true;
    input.escape.is_down = true;
    update_game(aiming, input, 0.016f);

    CHECK(aiming.mode == game_mode::walking);
    CHECK(aiming.swing.phase == swing_phase::idle);

    game_state addressing = make_initial_game_state();
    enter_addressing(addressing);

    input.reset_frame();
    input.escape.is_down = false;
    input.backspace.pressed = true;
    input.backspace.is_down = true;
    update_game(addressing, input, 0.016f);

    CHECK(addressing.mode == game_mode::walking);
    CHECK(addressing.swing.phase == swing_phase::idle);
    CHECK(should_cancel_shot_setup(game_mode::aiming, input));
    CHECK(should_cancel_shot_setup(game_mode::addressing, input));
    CHECK(!should_cancel_shot_setup(game_mode::walking, input));
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
    const terrain_sample terrain = sample_terrain_mesh(state.tuning.terrain_mesh_data,
                                                        glm::vec3(8.0f, 0.0f, 10.0f),
                                                        state.tuning.ground_y);
    state.ball.position = terrain.point + terrain.normal * state.ball.radius;

    input_state input;
    update_game(state, input, 0.016f);

    CHECK(state.mode == game_mode::walking);
    CHECK(state.player.position == player_position);
    CHECK(!can_interact_with_ball(state));
}

TEST_CASE("water drag slows ball more than fairway") {
    game_tuning tuning;
    tuning.wind.base_speed = 0.0f;
    tuning.wind.speed_variation = 0.0f;
    tuning.wind.speed_time_scale = 0.0f;
    tuning.wind.angle_variation = 0.0f;
    tuning.wind.angle_time_scale = 0.0f;
    tuning.wind.phase_angle_scale = 0.0f;
    tuning.terrain.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 20.0f)
    };
    tuning.terrain.width = 10.0f;
    tuning.terrain.sample_count = 16;
    tuning.ground_y = 0.0f;
    tuning.physics.drag_coeff = 0.01f;
    tuning.physics.magnus_coeff = 0.0f;
    tuning.physics.spin_decay = 0.0f;
    tuning.physics.water_drag_coeff = 0.35f;
    tuning.physics.water_spin_decay = 0.0f;
    tuning.zone_tuning.water_depth = 0.4f;
    tuning.ground_restitution = 0.0f;
    tuning.ground_friction = 0.0f;
    tuning.water_restitution = 0.0f;
    tuning.water_friction = 0.0f;

    material_zone water_zone;
    water_zone.type = material_zone_type::water;
    water_zone.center = glm::vec3(0.0f, 0.0f, 10.0f);
    water_zone.radius = 4.5f;
    water_zone.has_radius = true;
    tuning.course.material_zones = {water_zone};
    rebuild_cached_terrain_mesh(tuning);

    game_state wet_state;
    wet_state.tuning = tuning;
    wet_state.mode = game_mode::following_shot;
    wet_state.ball.position = glm::vec3(0.0f, 0.0f, 10.0f);
    wet_state.ball.velocity = glm::vec3(6.0f, 0.0f, 0.0f);

    game_state dry_state = wet_state;
    dry_state.tuning.course.material_zones.clear();
    rebuild_cached_terrain_mesh(dry_state.tuning);

    input_state input;
    update_game(wet_state, input, 0.016f);
    update_game(dry_state, input, 0.016f);

    CHECK(horizontal_speed(wet_state.ball.velocity) < horizontal_speed(dry_state.ball.velocity));
}
