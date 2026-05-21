#pragma once

#include "game/course_definition.h"
#include "game/game_tuning.h"
#include "game/round_state.h"
#include "game/save_data.h"
#include "game/swing.h"
#include "physics/ball_state.h"

#include <cstddef>
#include <string>

#include <glm/vec3.hpp>

struct input_state;

enum class game_mode {
    walking,
    aiming,
    addressing,
    following_shot
};

struct player_state {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
};

struct game_state {
    // ball_state.position is the center of the ball; see physics/ball_state.h.
    ball_state ball;
    player_state player;
    glm::vec3 shot_camera_position{0.0f};
    std::string asset_root;
    course_definition active_course;
    round_state round;
    save_data save;
    game_tuning tuning;
    game_mode mode = game_mode::walking;
    float aim_angle = 0.0f;
    std::size_t selected_club = 0;
    swing_state swing;
    int stroke_count = 0;
    float hole_time = 0.0f;
    bool rangefinder_active = false;
    float rangefinder_distance_meters = 0.0f;
    std::string rangefinder_distance_label;
    bool course_map_active = false;
};

game_state make_initial_game_state();
game_state make_initial_game_state(const std::string& asset_root);
void update_game(game_state& state, const input_state& input, float dt);
void retee_ball(game_state& state);
bool start_game_course(game_state& state, const course_definition& course);
bool complete_current_hole(game_state& state);
bool ball_is_in_cup(const game_state& state);
bool ball_is_moving(const ball_state& ball, const game_tuning& tuning);
bool ball_is_moving(const ball_state& ball);
bool can_interact_with_ball(const game_state& state);
bool rangefinder_should_show(game_mode mode, const input_state& input);
bool course_map_should_show(game_mode mode, const input_state& input);
bool should_cancel_shot_setup(game_mode mode, const input_state& input);
float compute_rangefinder_distance_meters(const glm::vec3& player_position,
                                          const glm::vec3& pin_anchor,
                                          float meters_per_world_unit);
std::string format_rangefinder_distance(float distance_meters);
