#pragma once

// Gameplay feel constants, loaded from assets/tuning/game_tuning.json by
// game/tuning_loader.h. Every field is required in the file; the zero
// defaults here only exist so the structs are plain values. Distances are in
// meters, times in seconds, angles in radians unless named otherwise.

#include "physics/physics_tuning.h"
#include "physics/terrain.h"

#include <glm/vec3.hpp>

struct world_scale_tuning {
    float meters_per_world_unit = 0.0f;
    float ball_physics_radius_meters = 0.0f;
    float ball_visual_radius_meters = 0.0f;
    // Drawn and holed at this radius: much larger than a real cup (arcade).
    float cup_radius_meters = 0.0f;
    float pin_visual_height_meters = 0.0f;
};

struct terrain_build_tuning {
    int min_sections = 0;           // terrain_spline::sample_count
    float ground_cell_size = 0.0f;  // spacing of the ground grid, the one surface of a play area
    // How far from a hole's edge the ground takes to ease into the course's land.
    float ground_blend_distance = 0.0f;
    terrain_zone_tuning zones;      // bunker and water carve depths
    float material_overlay_lift = 0.0f;
    // Greatest gap between the drawn zone shapes' vertices, so they bend with
    // the ground grid instead of sinking under it.
    float material_overlay_spacing = 0.0f;
};

struct ball_tuning {
    float stop_speed = 0.0f;  // below this (and grounded) the ball is at rest
    float ground_restitution = 0.0f;
    float ground_friction = 0.0f;  // per 1/60 s of contact, see physics/collision.h
    float water_restitution = 0.0f;
    float water_friction = 0.0f;
    float tree_restitution = 0.0f;
    float tree_friction = 0.0f;
    float roll_deceleration = 0.0f;  // m/s^2 while rolling (times the club's roll_friction_scale)
    float settle_speed = 0.0f;       // normal speed below which a grounded ball rolls instead of bouncing
};

struct swing_tuning {
    float meter_cycle_seconds = 0.0f;  // one full 0 -> 1 -> 0 swing meter cycle
    float min_power = 0.0f;
    float side_spin_scale = 0.0f;      // multiplies club_stats::side_spin
};

struct player_tuning {
    float walk_speed = 0.0f;
    float turn_rate = 0.0f;
    float aim_turn_rate = 0.0f;
    float ball_interact_radius = 0.0f;
    // How far the player stands from the ball: behind it when teeing up, to
    // its side (and address_back_distance behind) when addressing it.
    float ball_stand_off_distance = 0.0f;
    float address_back_distance = 0.0f;
    float emote_seconds = 0.0f;
};

struct cart_tuning {
    float speed = 0.0f;
    float turn_rate = 0.0f;
    float drift_turn_rate = 0.0f;
    float normal_damping = 0.0f;
    float drift_damping = 0.0f;
    float drift_duration = 0.0f;
    float drift_speed_boost = 0.0f;
    // A road counts as under the cart within half its width plus this margin
    // (and never less than min_road_reach).
    float road_reach_margin = 0.0f;
    float min_road_reach = 0.0f;
    // Hub only: multipliers on cart speed and control on and off roads.
    float road_speed_scale = 0.0f;
    float road_control_scale = 0.0f;
    float off_road_speed_scale = 0.0f;
    float off_road_control_scale = 0.0f;
};

// Camera rigs. "back" is against the aim direction, "side" is to the
// player's left (the address stance side).
struct camera_tuning {
    float fov_degrees = 0.0f;
    float walking_eye_height = 0.0f;
    float walking_look_distance = 0.0f;
    float aiming_back_distance = 0.0f;
    float aiming_eye_height = 0.0f;
    float aiming_look_distance = 0.0f;
    float aiming_look_height = 0.0f;
    float address_side_distance = 0.0f;
    float address_back_distance = 0.0f;
    float address_eye_height = 0.0f;
    float address_look_distance = 0.0f;
    float address_look_height = 0.0f;
    float follow_look_height = 0.0f;
    float transition_seconds = 0.0f;        // blend time on rig changes and teleports
    float transition_jump_distance = 0.0f;  // eye movement per frame that counts as a teleport
};

// The dotted arc shown while aiming.
struct aim_preview_tuning {
    float step_seconds = 0.0f;
    int max_points = 0;
};

// The trail drawn behind a ball in flight.
struct flight_path_tuning {
    glm::vec3 color{0.0f};
    float alpha = 0.0f;
    float min_point_spacing = 0.0f;
    int max_points = 0;
    float line_width = 0.0f;
};

// The "+25 XP" popups.
struct xp_drop_tuning {
    float lifetime_seconds = 0.0f;
    int min_visible_xp = 0;  // smaller gains are pooled until they reach this
};

struct game_tuning {
    world_scale_tuning scale;
    terrain_build_tuning terrain;
    physics_tuning physics;
    wind_tuning wind;
    ball_tuning ball;
    swing_tuning swing;
    player_tuning player;
    cart_tuning cart;
    camera_tuning camera;
    aim_preview_tuning aim_preview;
    flight_path_tuning flight_path;
    xp_drop_tuning xp_drops;
};
