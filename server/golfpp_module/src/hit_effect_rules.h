#pragma once

// The running_effect rows a ball that hits a golfer changes: the event's
// earlier rows end where its effects start, when the ball reaches them
// (game/effect_catalog.h effects_after_event), so a hit refreshes the effect
// and never stacks, and the time before the ball arrives is as it was.
// Plain C++ for the reducer and the native tests.

#include "game/effect_catalog.h"
#include "game/effects.h"

#include <cstdint>
#include <utility>
#include <vector>

// A running_effect row of one account, as the reducer reads it.
struct running_effect_ref {
    std::uint64_t id = 0;
    active_effect effect;
};

struct hit_effect_change {
    std::vector<std::uint64_t> remove_ids;  // rows that had not begun by the new start
    std::vector<std::pair<std::uint64_t, std::int64_t>> end_at;  // row id, its new ends_at_ms
    std::vector<active_effect> add;
};

// Starting `event` at `hit_ms` (UTC milliseconds) on an account whose rows
// are `rows`; other sources' rows are left alone.
hit_effect_change effects_started_by_hit(const std::vector<running_effect_ref>& rows, const event_effect_definition& event,
                                         std::int64_t hit_ms);
