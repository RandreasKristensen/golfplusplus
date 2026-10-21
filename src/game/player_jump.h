#pragma once

// A hop on foot: up at player.jump_speed, pulled down by player.jump_gravity,
// in world height from where it took off, until it comes down on the ground
// under it: ground passing underneath (a tee box's edge) never lifts it.
// Movement and looks only, never scored. Mine and other players' hops
// (game/remote_players.h) follow the same arc.

#include "game/game_tuning.h"

// Higher than this over the ground a player is in the air: a motion sent
// from there tells others I hopped (world units).
inline constexpr float airborne_height = 0.05f;

// Seconds from taking off to landing on level ground.
inline float hop_seconds(const player_tuning& tuning) {
    return tuning.jump_gravity > 0.0f ? 2.0f * tuning.jump_speed / tuning.jump_gravity : 0.0f;
}

// Height over where it took off `seconds` into a hop: past the hop's
// length it goes on falling below it, onto lower ground.
inline float hop_rise(const float seconds, const player_tuning& tuning) {
    return tuning.jump_speed * seconds - 0.5f * tuning.jump_gravity * seconds * seconds;
}

// A hop off `takeoff_y`, `seconds` in, has come down onto ground at
// `ground_y` (or met ground rising faster than it).
inline bool hop_landed(const float takeoff_y, const float seconds, const float ground_y, const player_tuning& tuning) {
    constexpr float landing_tolerance = 1.0e-4f;
    return seconds > 0.0f && takeoff_y + hop_rise(seconds, tuning) <= ground_y + landing_tolerance;
}
