#pragma once

#include "game/club_definition.h"
#include "physics/physics_tuning.h"

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

struct sandbox_course {
    glm::vec3 tee_position{0.0f, 0.0f, 0.0f};
    glm::vec3 pin_position{0.0f, 0.0f, 0.0f};
    float cup_radius = 0.0f;
    float extent = 0.0f;
};

struct game_tuning {
    sandbox_course course;
    std::vector<club_definition> clubs;
    physics_tuning physics;
    wind_tuning wind;
    std::uint32_t wind_seed = 0U;
    float aim_turn_rate = 0.0f;
    float min_swing_power = 0.0f;
    float launch_side_spin_scale = 0.0f;
    float stop_speed = 0.0f;
    float ground_y = 0.0f;
    float ground_restitution = 0.0f;
    float ground_friction = 0.0f;
    float ground_roll_friction = 0.0f;
    float ground_settle_speed = 0.0f;
    float ball_interact_radius = 0.0f;
    float player_walk_speed = 0.0f;
    float player_turn_rate = 0.0f;
    float player_stand_off_distance = 0.0f;
};

// Central gameplay feel tuning. Edit this factory's values in game_tuning.cpp.
game_tuning default_game_tuning();
