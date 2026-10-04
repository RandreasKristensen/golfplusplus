#pragma once

// When the local player's movement goes to the server in online play. Motion
// is sparse: others carry it on with extrapolate_motion (game/net_types.h), so it is sent when
// the intent changes (zone, mode, starting, stopping or reversing a move or a
// turn, a cart getting on or off a road), as a heartbeat while moving, and as a correction when that
// extrapolation drifts too far from where the player really is. Never more
// often than net.motion_min_interval_seconds.

#include "game/game_tuning.h"
#include "game/net_types.h"

#include <optional>

#include <glm/vec3.hpp>

struct game_state;

// Slower than these counts as not moving or not turning: no motion update, no
// movement XP for a parked cart.
inline constexpr float still_speed = 0.05f;      // world units/s
inline constexpr float still_turn_rate = 0.01f;  // rad/s

struct motion_sync_state {
    std::optional<net_motion> last_sent;
    float since_sent = 0.0f;  // seconds
};

bool should_send_motion(const motion_sync_state& sync, const net_motion& now, const net_tuning& tuning);

// The local player's motion this frame.
net_motion current_motion(const game_state& state);

// Online: pushes a motion command when should_send_motion says so, except
// while a shot plays or the ball blends to the server's rest (the server may
// already have the player back in the hub). Offline: nothing.
void sync_motion(game_state& state, float dt);

// Online: pushes the current motion now, whatever the interval, so the
// server knows where the player stands before an action it checks the
// reach of (entering a hole, claiming a collectible). Offline: nothing.
void send_motion_now(game_state& state);
