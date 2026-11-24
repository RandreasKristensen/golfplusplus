#pragma once

// When each movement step happened. A movement batch carries the shared-clock
// time of its first step; every later step is that plus whole step lengths
// (game/movement_step.h, 1/120 s, so 25/3 ms: step i is at first + i steps to
// the nearest whole millisecond, so no step of a batch drifts). The server refuses a
// batch in its future or overlapping steps it already played, and a batch
// whose steps all ended before the rewind window counts as standing for
// contacts (ball_contact.h) but is still played. Pure.

#include "game/game_tuning.h"
#include "game/movement_step.h"

#include <cstdint>
#include <optional>

static_assert(move_step_seconds == 1.0f / 120.0f, "step_timeline.h counts a step as 25/3 ms");

// The time of step `index` of a batch whose step 0 was at `first_time_ms`.
std::int64_t step_time_ms(std::int64_t first_time_ms, std::uint64_t index);

// The last step a mover played and when (shared clock, ms).
struct timeline_anchor {
    std::uint64_t step = 0;
    std::int64_t time_ms = 0;
};

// A batch of `steps` steps numbered from `first_step`, its first at
// `first_time_ms` as the client claims.
struct timed_batch {
    std::uint64_t first_step = 0;
    std::uint32_t steps = 0;
    std::int64_t first_time_ms = 0;
};

enum class timeline_verdict : std::uint8_t {
    accepted,
    future,   // its last step is later than the server's clock by more than server.timing_slack_seconds
    overlap   // its first new step is earlier than the last played step plus the steps between them
};

struct timeline_result {
    timeline_verdict verdict = timeline_verdict::accepted;
    bool repeated = false;  // every step was played already: nothing new, `anchor` unchanged
    // The batch's steps all end before now minus server.contact_rewind_seconds.
    bool late = false;
    timeline_anchor anchor;  // the last step played, after this batch
};

// Places `batch` after `previous` (none: the mover's first), the server's clock
// being `now_ms`. The steps between the last played and the batch's first new
// step take at least their length rounded down, less a millisecond, so a
// client's rounding of its own times is never refused for it.
timeline_result place_batch(const std::optional<timeline_anchor>& previous, const timed_batch& batch, std::int64_t now_ms,
                            const server_tuning& tuning);
