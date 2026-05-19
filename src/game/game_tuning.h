#pragma once

#include "game/club_definition.h"
#include "game/hole_data.h"
#include "physics/physics_tuning.h"
#include "physics/terrain.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

struct sandbox_course {
    std::string id;
    std::string name;
    int par = 3;
    glm::vec3 tee_position{0.0f, 0.0f, 0.0f};
    glm::vec3 pin_position{0.0f, 0.0f, 0.0f};
    float cup_radius = 0.0f;
    float extent = 0.0f;
    hole_spline spline;
    std::vector<material_zone> material_zones;
};

struct world_scale_tuning {
    float meters_per_world_unit = 1.0f;
    float ball_physics_radius_meters = 0.021335f;
    float ball_mass_kg = 0.04593f;
    float cup_physics_radius_meters = 0.053975f;
    float ball_visual_radius_meters = 0.10f;
    float cup_visual_radius_meters = 0.10f;
    float pin_visual_height_meters = 2.10f;
};

struct game_tuning {
    sandbox_course course;
    world_scale_tuning scale;
    terrain_spline terrain;
    terrain_mesh terrain_mesh_data;
    terrain_zone_tuning zone_tuning;
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
    float water_restitution = 0.0f;
    float water_friction = 0.0f;
    float ground_roll_friction = 0.0f;
    float ground_settle_speed = 0.0f;
    float ball_interact_radius = 0.0f;
    float player_walk_speed = 0.0f;
    float player_turn_rate = 0.0f;
    float player_stand_off_distance = 0.0f;
};

// Central gameplay feel tuning. Edit this factory's values in game_tuning.cpp.
game_tuning default_game_tuning();
