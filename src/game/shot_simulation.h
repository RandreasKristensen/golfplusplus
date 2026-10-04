#pragma once

// One golf shot, simulated from launch to rest in fixed steps. Pure, like
// physics/: the same input on the same course gives the same result, so a
// server can replay a client's shot. game_state shows a result over real time
// (play_shot), whatever the frame rate.

#include "game/club_definition.h"
#include "game/game_tuning.h"
#include "game/play_area.h"
#include "game/reward_rules.h"
#include "physics/ball_state.h"
#include "physics/terrain.h"
#include "physics/tree_collision.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

// The fixed step, the longest a shot may run, and how often the trajectory
// keeps a point. Part of the simulation itself, not feel: changing any of
// them changes where every shot lands.
inline constexpr float shot_step_seconds = 1.0f / 120.0f;
inline constexpr int shot_max_steps = 120 * 60;
inline constexpr int shot_steps_per_trajectory_point = 2;

// Everything the player chose for a shot.
struct shot_input {
    glm::vec3 ball_start{0.0f};
    float aim_angle = 0.0f;  // see yaw_direction in physics/vector_math.h
    std::string club_id;
    float power = 0.0f;  // swing meter power, clamped to [swing.min_power, 1]
    bool cigarette_active = false;
    float wind_time = 0.0f;  // seconds since the hole started, at launch
};

// The hole a shot is played on, in play-area coordinates.
struct shot_hole {
    glm::vec3 pin{0.0f};  // the cup centre, on the terrain
    std::uint32_t wind_seed = 0;
};

// What a shot plays on, borrowed for one simulation.
struct shot_course {
    const play_area& area;
    const std::vector<tree_body>& trees;  // standing on `area` (static_anchor_cache::trees)
    shot_hole hole;
};

enum class shot_event_kind {
    land,
    tree_hit
};

// Something to hear during playback.
struct shot_event {
    float time = 0.0f;  // seconds after launch
    shot_event_kind kind = shot_event_kind::land;
    terrain_material material = terrain_material::fairway;  // land only
};

struct shot_result {
    glm::vec3 rest_position{0.0f};  // in the cup when holed
    bool holed = false;
    float duration = 0.0f;  // seconds from launch to rest
    // The ball centre every shot_steps_per_trajectory_point steps from
    // launch; the last point is where the simulation stopped, at `duration`.
    std::vector<glm::vec3> trajectory;
    std::vector<shot_event> events;  // in time order
};

// One fixed step of a moving ball, and what happened during it.
struct shot_step {
    ball_state ball;
    terrain_sample ground;  // under the ball after the step
    bool landed = false;
    bool hit_tree = false;
};

// The club's stats with the cigarette modifiers from rewards.json applied.
club_stats shot_club_stats(const club_stats& club, bool cigarette_active, const reward_rules& rewards);

// The ball leaving `position` along `aim_angle`, at the club's loft and
// `power` (clamped to [swing.min_power, 1]) of its full speed.
ball_state launch_ball(const glm::vec3& position, float aim_angle, const club_stats& stats, float power, const game_tuning& tuning);

// Flight, ground contact, trees and rolling friction over `dt`. `ground` is
// the terrain under `ball` (the previous step's `ground`).
shot_step step_shot(const ball_state& ball,
                    const terrain_sample& ground,
                    const shot_course& course,
                    const club_stats& stats,
                    const game_tuning& tuning,
                    float wind_time,
                    float dt);

// True when a grounded ball is slower than ball.stop_speed.
bool ball_at_rest(const ball_state& ball, const terrain_sample& ground, const ball_tuning& tuning);

// Steps `launched` until it rests, drops into the cup or hits shot_max_steps.
shot_result simulate_ball(const ball_state& launched,
                          const club_stats& stats,
                          const shot_course& course,
                          const game_tuning& tuning,
                          float wind_time);

// The whole shot. An unknown club leaves the ball where it is.
shot_result simulate_shot(const shot_input& input,
                          const shot_course& course,
                          const game_tuning& tuning,
                          const std::vector<club_definition>& clubs,
                          const reward_rules& rewards);

// Where the ball is `time` seconds after launch, between trajectory points.
glm::vec3 shot_position_at(const shot_result& result, float time);
