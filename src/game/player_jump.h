#pragma once

// Up and down on foot: Space hops off the ground at player.jump_speed, and
// in the air (hopping, or off an edge higher than player.step_up) a player
// falls at player.jump_gravity until they come down on the surface under
// them (physics/body_collision.h settle_vertical). Movement and looks only,
// never scored. Mine and other players' (game/remote_players.h) move alike.

#include "game/game_tuning.h"
#include "physics/body_collision.h"

// Higher than this over the ground a player is in the air: a motion sent
// from there tells others I hopped (world units).
inline constexpr float airborne_height = 0.05f;

// Seconds from taking off to landing on level ground.
inline float hop_seconds(const player_tuning& tuning) {
    return tuning.jump_gravity > 0.0f ? 2.0f * tuning.jump_speed / tuning.jump_gravity : 0.0f;
}

// `body` hopping: off the ground it takes off; in the air it goes on.
inline vertical_body hopped(const vertical_body& body, const player_tuning& tuning) {
    return body.grounded ? vertical_body{body.y, tuning.jump_speed, false} : body;
}

// `body` (its feet at `y`) over `dt` above a surface at `surface`.
inline vertical_body settle_on_foot(const vertical_body& body, const float y, const float surface,
                                    const player_tuning& tuning, const float dt) {
    vertical_body at = body;
    at.y = y;
    return settle_vertical(at, surface, tuning.step_up, tuning.jump_gravity, dt);
}
