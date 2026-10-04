#pragma once

// The other players in my room as this client shows them (presentation
// only, never results): each carried on along their last motion with the
// game's own movement (extrapolate_motion), at most
// net.remote_extrapolation_seconds, on the ground, with a new motion blending
// in; their shots played back from the server's events; their balls.
// While the connection is down everything stands still.

#include "game/game_state.h"
#include "game/net_types.h"

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/vec3.hpp>

void update_remote_players(game_state& state, float dt);

// Plays another player's shot from the server's event, ending where the
// server says it rests. Replaces their shot still playing, if any.
void start_remote_shot(game_state& state, const room_shot& shot);

// Where a remote shot's ball is `shot.elapsed` into it.
glm::vec3 remote_shot_position(const remote_shot& shot);
bool remote_shot_playing(const remote_shot& shot);

// Others' balls: where their shot is while it plays, else where it rests.
struct shown_ball {
    std::uint64_t account_id = 0;
    glm::vec3 position{0.0f};
};
std::vector<shown_ball> remote_balls(const game_state& state);

// The zone I play in: hub_zone, or the hole's index.
int local_zone(const game_state& state);

// The nearest other player in my zone within `distance` of me (XZ), as shown.
std::optional<std::uint64_t> nearest_player(const game_state& state, float distance);
