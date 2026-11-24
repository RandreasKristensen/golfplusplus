#pragma once

// What a ball that hits a person does to them: where on the body it struck,
// then a seeded roll of none, dazed or knocked out from the ball's speed.
// Pure and shared by the server (which rolls it for everyone online) and the
// offline game. The roll uses the shot's seed in a stream apart from the
// flight's and the swing's, so replays agree and the flight is unchanged.
// A ball that hits someone is played as it lies, with no penalty (Rule 11.1).

#include "game/effect_catalog.h"
#include "game/game_tuning.h"
#include "game/shot_simulation.h"
#include "game/shot_snapshot.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// The event effect (game/effect_catalog.h) a hit starts.
inline constexpr const char* dazed_effect_id = "dazed";

enum class body_band {
    legs,
    body,
    head
};

enum class hit_outcome_kind {
    none,
    dazed,
    knocked_out
};

// Which part of a person of height `person_height_m` a contact at
// `contact_height_m` above their feet struck.
body_band contact_band(float contact_height_m, float person_height_m, const ball_hit_tuning& tuning);

// The 0..1 share of a full drive's speed `speed_mps` counts for.
float hit_speed_share(float speed_mps, const ball_hit_tuning& tuning);

// The outcome of hit number `hit_index` of the shot with `seed`. Under
// min_speed_mps nothing happens; a head hit knocks out with a chance of
// head_knockout_max times the speed share, never for `self_hit` (the
// shooter struck by their own ball); every other hit above the minimum
// dazes.
hit_outcome_kind hit_outcome(float speed_mps, float contact_height_m, float person_height_m, bool self_hit,
                             const ball_hit_tuning& tuning, std::uint64_t seed, int hit_index);

// What a shot's hit on a person came to: someone hurt, when. The server
// (account set) and the offline game (account 0: me) take the same list.
struct hit_consequence {
    std::size_t entry = 0;  // in the shot's snapshot
    std::uint64_t account = 0;
    hit_outcome_kind outcome = hit_outcome_kind::dazed;  // never none
    float time = 0.0f;  // seconds after launch
};

// The outcomes of `result`'s person hits on `snapshot`'s people that are not
// none, in time order. Hit number i of the shot rolls with `seed` and i; the
// shooter's own hit never knocks out.
std::vector<hit_consequence> hit_consequences(const shot_result& result, const std::vector<snapshot_entry>& snapshot,
                                              const game_tuning& tuning, std::uint64_t seed);

// `running` once each consequence's effect starts at `start_ms` plus its
// time: dazed, for a knockout too. Offline's way; the server writes rows
// (server hit_effect_rules.h) from the same consequences.
std::vector<active_effect> effects_after_hits(std::vector<active_effect> running, const std::vector<hit_consequence>& hits,
                                              const event_effect_definition& dazed, std::int64_t start_ms);
