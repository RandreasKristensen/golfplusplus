#pragma once

// Free practice ball placement and pickup, shared by offline play and the server.
// These decisions read the one surface and never move a player or touch progress.
// The hub's balls are for fooling about, so a drop is never refused there: a
// golfer has one ball, and the drop replaces it wherever it lies or flies.
#include "game/ball_kind.h"
#include "game/place_definition.h"
#include "game/play_area.h"
#include <optional>

// Where a ball dropped from `requested` rests, on the one surface under it;
// from `accepted` (the server's word on where the golfer is) when `requested`
// is not a point within warmup_reach_meters of it. None off the hub: a
// course's balls are scored.
std::optional<glm::vec3> practice_drop_position(const play_area& area, place_kind kind, const glm::vec3& accepted,
    const glm::vec3& requested, const game_tuning& tuning);
bool practice_pickup_in_reach(ball_kind kind, bool resting, bool on_foot, const glm::vec3& accepted,
    const glm::vec3& ball, const game_tuning& tuning);
struct game_state;
void request_practice_drop(game_state& state);
bool pick_up_practice_ball(game_state& state);
