#pragma once

#include "game/swing.h"
#include "physics/ball_state.h"
#include "physics/club_stats.h"

#include <cstddef>
#include <cstdint>
#include <vector>

struct input_state;

struct game_state {
    ball_state ball;
    float aim_angle = 0.0f;
    std::size_t selected_club = 0;
    std::vector<club_stats> clubs;
    swing_state swing;
    int stroke_count = 0;
    std::uint32_t wind_seed = 42U;
    float hole_time = 0.0f;
};

game_state make_initial_game_state();
void update_game(game_state& state, const input_state& input, float dt);
bool ball_is_moving(const ball_state& ball);
