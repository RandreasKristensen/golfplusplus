#pragma once

// The other players in my room as this client shows them (presentation
// only, never results): each carried on along their last motion with the
// game's own movement (extrapolate_motion), at most
// net.remote_extrapolation_seconds, on the ground, with a new motion blending
// in (over cart.step_seconds stepping into or out of a cart), turning as they
// turn (aiming too); riding in a cart, sat in its passenger seat as the cart
// is shown (game/golf_cart.h); what they smoke and drink, with the local
// player's timings; addressing their ball, their club as their own game
// shows it; their shots played back from the server's events; their balls.
// While the connection is down everything stands still.

#include "game/game_state.h"
#include "game/net_types.h"

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/vec3.hpp>

void update_remote_players(game_state& state, float dt);

// A shot the server played, simulated here from its event as the server
// simulated it: its record (setup, dice and snapshot), the golfer as the
// server played them, in the weather at its shot_at with the flags as they
// were, bit for bit.
shot_result room_shot_result(const game_state& state, const room_shot& shot);

// Plays another player's shot from the server's event, ending where the
// server says it rests: the ball leaves now, as it left when they hit it,
// and the swing it was hit from is over. Replaces their shot still playing,
// if any. Its strike and its events are heard from where they happen.
void start_remote_shot(game_state& state, const room_shot& shot);

// Plays another player's use of an item from the server's event, as mine
// plays, heard from their mouth: not restarted while it plays.
void start_remote_item_use(game_state& state, const room_item_use& use);

// Another player addressing their ball (motion_mode::address or swing), as
// shown: their ball, their aim, and their club raised as far as
// `club_raise` (as append_swing_club draws mine). Their swing plays from
// when it reached the server (game/swing.h), a full one at the full
// swing's tempo: their club and setup aren't sent.
struct remote_address {
    glm::vec3 ball_position{0.0f};
    float aim_angle = 0.0f;
    float club_raise = 0.0f;
};
// Nothing unless they address their ball, or once the shot of that swing is hit.
std::optional<remote_address> remote_address_pose(const game_state& state, std::uint64_t account);

// Where a remote shot's ball is `shot.elapsed` into it.
glm::vec3 remote_shot_position(const remote_shot& shot);
bool remote_shot_playing(const remote_shot& shot);

// Others' balls: where their shot is while it plays, else where it rests.
struct shown_ball {
    std::uint64_t account_id = 0;
    glm::vec3 position{0.0f};
};
std::vector<shown_ball> remote_balls(const game_state& state);

// What another player is to me, which colours their name tag.
enum class player_relationship { unknown, grouped };
player_relationship relationship_to(const game_state& state, const room_player& player);

// The nearest other player in my room within `distance` of me (XZ), as shown,
// wherever either of us plays.
std::optional<std::uint64_t> nearest_player(const game_state& state, float distance);
