#include "game/ball_hit_rules.h"

#include "game/shot_dice.h"

#include <cmath>
#include <utility>
#include "physics/vector_math.h"

namespace {
// The dice stream of hit rolls: the flight's steps count up from 0 and the
// swing's is shot_strike_step (-1).
constexpr int hit_dice_step = -2;
}

body_band contact_band(const float contact_height_m, const float person_height_m, const ball_hit_tuning& tuning) {
    if (contact_height_m >= person_height_m * (1.0f - tuning.head_band_share)) {
        return body_band::head;
    }
    return contact_height_m < person_height_m * tuning.legs_band_share ? body_band::legs : body_band::body;
}

float hit_speed_share(const float speed_mps, const ball_hit_tuning& tuning) {
    return clamp01((speed_mps - tuning.putt_speed_mps) / (tuning.drive_speed_mps - tuning.putt_speed_mps));
}

hit_outcome_kind hit_outcome(const float speed_mps, const float contact_height_m, const float person_height_m,
                             const bool self_hit, const ball_hit_tuning& tuning, const std::uint64_t seed,
                             const int hit_index) {
    if (speed_mps < tuning.min_speed_mps) {
        return hit_outcome_kind::none;
    }
    const bool head = contact_band(contact_height_m, person_height_m, tuning) == body_band::head;
    if (head && !self_hit) {
        const float chance = tuning.head_knockout_max * hit_speed_share(speed_mps, tuning);
        if (dice_uniform(seed, hit_dice_step, static_cast<std::uint64_t>(hit_index)) < chance) {
            return hit_outcome_kind::knocked_out;
        }
    }
    return hit_outcome_kind::dazed;
}

std::vector<hit_consequence> hit_consequences(const shot_result& result, const std::vector<snapshot_entry>& snapshot,
                                              const game_tuning& tuning, const std::uint64_t seed) {
    std::vector<hit_consequence> consequences;
    for (std::size_t i = 0; i < result.person_hits.size(); ++i) {
        const person_hit& hit = result.person_hits[i];
        if (hit.entry >= snapshot.size() || snapshot[hit.entry].kind != snapshot_kind::person) {
            continue;
        }
        const snapshot_entry& person = snapshot[hit.entry];
        const hit_outcome_kind outcome =
            hit_outcome(hit.speed_mps, hit.contact_height_m, tuning.rangefinder.person_height * tuning.scale.meters_per_world_unit,
                        person.shooter, tuning.ball_hits, seed, static_cast<int>(i));
        if (outcome != hit_outcome_kind::none) {
            consequences.push_back(hit_consequence{hit.entry, person.account, outcome, hit.time});
        }
    }
    return consequences;
}

std::vector<active_effect> effects_after_hits(std::vector<active_effect> running, const std::vector<hit_consequence>& hits,
                                              const event_effect_definition& dazed, const std::int64_t start_ms) {
    for (const hit_consequence& hit : hits) {
        running = effects_after_event(std::move(running), dazed,
                                      start_ms + static_cast<std::int64_t>(std::llround(hit.time * 1000.0f)));
    }
    return running;
}
