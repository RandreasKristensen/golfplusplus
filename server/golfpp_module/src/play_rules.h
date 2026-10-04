#pragma once

// Rules for playing online, as plain functions the module's reducers call
// and the native tests check: which room a player joins, how far they may
// move and what that earns, what is in reach, and whether a shot is valid.
// The server decides shots and progress; movement comes from the client and
// is only checked against what the game allows. Errors are the ids in
// server_errors.h.

#include "game/game_tuning.h"
#include "game/net_types.h"
#include "game/shot_simulation.h"
#include "server_content.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

// --- Rooms ---------------------------------------------------------------------

struct room_candidate {
    std::uint64_t room_id = 0;
    int player_count = 0;
};

// The fullest room with space, so rooms fill before new ones open; the
// oldest (lowest id) among equals. nullopt when every room is full.
std::optional<std::uint64_t> choose_room(const std::vector<room_candidate>& rooms, int capacity);

// --- Movement ------------------------------------------------------------------

// Clients send motion_mode (game/net_types.h) as its underlying number; any
// below this is a valid mode.
inline constexpr std::uint8_t motion_mode_count = static_cast<std::uint8_t>(motion_mode::aim) + 1;

// The fastest the game moves a player in `mode`: walking speed on foot,
// the fastest cart in the cart.
float mode_speed(std::uint8_t mode, const game_tuning& tuning);

struct motion_check {
    bool allowed = false;
    // The motion slack left after this move: server.motion_distance_slack at
    // most, refilling at that many world units per second. A move may go
    // that much further than its speed allows, so the slack covers lag
    // without adding up over many quick calls.
    float slack_left = 0.0f;
    // How far the move counts for movement XP, in previous.mode (the mode the
    // stretch was covered in): the path `previous` traced when the move
    // ended where it leads, else the straight line; never more than that
    // mode's own speed over the time, as offline.
    float earned_distance = 0.0f;  // world units
};

// Moving from `previous.position` to `to` (XZ) in `seconds` (at most
// server.max_motion_gap_seconds count), having moved as `previous` (the last
// accepted motion): the faster of the two modes' speeds times
// server.motion_speed_scale, plus the slack left.
motion_check check_motion(const net_motion& previous,
                          const glm::vec3& to,
                          float seconds,
                          std::uint8_t mode,
                          float slack_left,
                          const game_tuning& tuning);

// What others extrapolate an avatar with is limited to what the game can
// produce: speed to `mode`'s (times server.motion_speed_scale), the turn
// rate to the fastest the player, aim or cart turns.
float clamp_motion_speed(std::uint8_t mode, float speed, const game_tuning& tuning);
float clamp_turn_rate(float turn_rate, const game_tuning& tuning);

// Which movement XP a move earns, as offline: walking on foot, the cart and
// drift rates only in a cart on a road.
struct movement_earnings {
    bool walking = false;
    bool cart = false;
    bool drift = false;
};
movement_earnings movement_earns(std::uint8_t mode, bool on_road);

// Within an interaction `radius` of `target` (XZ), plus server.interact_slack.
bool within_interact_reach(const glm::vec3& position, const glm::vec3& target, float radius, const server_tuning& tuning);

// --- Shots ---------------------------------------------------------------------

// What a client asks for: its shot_input without the ball start, which the
// server takes from its own ball.
struct shot_request {
    int stroke = 0;  // the stroke this shot is, from 1
    float aim_angle = 0.0f;
    std::string club_id;
    float power = 0.0f;
    bool cigarette_active = false;
    float wind_time = 0.0f;
};

// The server's ball on the hole being played.
struct server_ball {
    glm::vec3 position{0.0f};
    int stroke_count = 0;
    float last_wind_time = 0.0f;
};

struct shot_check {
    std::optional<shot_input> input;  // what to simulate, when the shot is valid
    std::string error;
};

// The next stroke, hit from beside the ball (player.ball_interact_radius,
// plus slack), with a known club, an aim wrapped into [-pi, pi]
// (wrap_angle), finite power (clamped to [swing.min_power, 1]), and a wind
// time within server.wind_time_slack_seconds of the time on the hole and no
// earlier than the last shot's. A cigarette must be lit if the shot says so.
shot_check check_shot(const shot_request& request,
                      const server_ball& ball,
                      const glm::vec3& player_position,
                      float seconds_on_hole,
                      bool cigarette_lit,
                      const server_content& content);
