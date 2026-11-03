#pragma once

// A player's movement as the server plays it, by input: the fixed steps a
// client sends (game/movement_step.h) are stepped from the server's own
// state with the game's own steps, so where a player is, how far they went
// and what that earns are the server's. Positions a client claims count
// only where nothing is stepped (at the ball, riding along) or where it was
// put without a step (out of a seat), each costing the time it would take
// to get there. Plain C++ for the reducer, the native tests and the sync
// harness. Errors are the ids in server_errors.h.

#include "game/game_tuning.h"
#include "game/movement_step.h"
#include "game/net_types.h"
#include "play_rules.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

// The most fixed steps one motion may carry: twice what
// server.max_motion_gap_seconds holds.
std::uint32_t max_steps_per_motion(const server_tuning& tuning);

// What the server keeps of a player's movement (its motion_state row).
struct mover_state {
    motion_mode mode = motion_mode::idle;
    glm::vec3 position{0.0f};  // on foot; sat, the seat
    vertical_body vertical;
    std::uint64_t step = 0;    // the last fixed step played or passed over
    // Seconds of movement left to spend: real time since, saved up to
    // server.max_motion_gap_seconds. A step spends its length; a claimed
    // move the time it takes at its mode's speed.
    float credit_seconds = 0.0f;
    float slack_left = 0.0f;  // check_motion's
    double client_time = 0.0;  // the last motion accepted
    // My cart's motion while I drive it; its pose is its row's.
    glm::vec2 cart_velocity{0.0f};
    float cart_yaw_rate = 0.0f;
    float cart_lift_seconds = 0.0f;
    vertical_body cart_vertical;
};

// Standing at `position` (placed, joined, out of a seat): nothing moving,
// my cart's motion stopped.
mover_state placed_mover(mover_state mover, const glm::vec3& position);

// One motion: its steps (numbered on from first_step, the first on foot
// from `start`) and where the client has itself after them.
struct move_request {
    net_motion motion;
    std::uint64_t first_step = 0;
    glm::vec3 start{0.0f};
    std::vector<move_run> runs;
};

// What the steps meet and what moves them: the room as its rows have it and
// what the player's items do now.
struct move_world {
    const play_area& area;
    const std::vector<course_world_cart_road>& roads;
    const game_tuning& tuning;
    float walk_speed = 0.0f;  // player.walk_speed with what items do now
    cart_spec spec;           // effective_cart_spec
    float grip_scale = 1.0f;
    float wetness = 0.0f;
    std::vector<solid> walker_obstacles;  // walker_obstacles: every cart in the room
    std::vector<solid> cart_obstacles;    // cart_obstacles_of: the others
    movement_limits limits;
    // My ball at rest after its shot: addressing it may step me round it.
    std::optional<glm::vec3> own_ball;
};

struct move_outcome {
    std::string error;       // refused: nothing changes
    bool repeated = false;   // the motion already accepted, sent again: nothing changes
    mover_state mover;
    std::optional<room_cart> cart;  // my cart's row, when I drove it or left its wheel
    net_motion shown;               // the avatar row's motion
    float walked_meters = 0.0f;     // on foot, for movement XP
    float road_meters = 0.0f;       // at my wheel on a cart road
    bool overturned = false;        // my cart tipped or sank under me
};

// Plays `request` for `account` from `mover`, `seconds` after the last
// motion, with my cart's row `cart` (if I have one). Steps already played
// are passed over (a motion sent again), steps never received count as
// standing, and steps beyond the time saved up are passed over unplayed.
// On foot the steps start from `start` when it is within
// server.step_start_tolerance and the time to get there; at the wheel from
// my cart's row and my mover's cart motion. Walking steps take me off my
// cart's wheel; a cart going over puts me beside it.
move_outcome play_move(const mover_state& mover, const std::optional<room_cart>& cart, std::uint64_t account,
                       const move_request& request, float seconds, const move_world& world);

// Of `meters` of movement, those still earning XP this hour, with `used`
// already earned and `per_hour` the most; `used` takes them.
float earning_meters(float meters, float& used, float per_hour);
