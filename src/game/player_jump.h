#pragma once

// A hop on foot: up at player.jump_speed, pulled down by player.jump_gravity,
// back on the ground it is over. Movement and looks only, never scored. Mine
// and other players' hops (game/remote_players.h) follow the same arc.

#include "game/game_tuning.h"

// Higher than this over the ground a player is in the air: a motion sent
// from there tells others I hopped (world units).
inline constexpr float airborne_height = 0.05f;

// Seconds from taking off to landing.
inline float hop_seconds(const player_tuning& tuning) {
    return tuning.jump_gravity > 0.0f ? 2.0f * tuning.jump_speed / tuning.jump_gravity : 0.0f;
}

// Height over the ground `seconds` after taking off: 0 before and after the hop.
inline float hop_height(const float seconds, const player_tuning& tuning) {
    if (seconds <= 0.0f || seconds >= hop_seconds(tuning)) {
        return 0.0f;
    }
    return tuning.jump_speed * seconds - 0.5f * tuning.jump_gravity * seconds * seconds;
}
