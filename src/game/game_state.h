#pragma once

#include "game/game_tuning.h"
#include "game/swing.h"
#include "physics/ball_state.h"

#include <cstddef>

struct input_state;

struct game_state {
    ball_state ball;
    game_tuning tuning;
    float aim_angle = 0.0f;
    std::size_t selected_club = 0;
    swing_state swing;
    int stroke_count = 0;
    float hole_time = 0.0f;
};

game_state make_initial_game_state();
void update_game(game_state& state, const input_state& input, float dt);
void retee_ball(game_state& state);
bool ball_is_moving(const ball_state& ball, const game_tuning& tuning);
bool ball_is_moving(const ball_state& ball);
