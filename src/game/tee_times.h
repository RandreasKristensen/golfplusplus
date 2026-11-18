#pragma once

// The UTC tee times a course offers: one every tee_interval_minutes from the
// next whole interval, up to the end of the hub's in-game day
// tee_book_days_ahead after today, so the book reaches today and tomorrow on
// the hub clock and no further. They are a function of the clock and the
// tuning alone, so the server keeps a tee_slot row only for a time someone
// has booked, and the tee book lists every other time itself.
#include "game/game_tuning.h"
#include "game/sky_clock.h"

#include <cmath>
#include <cstdint>
#include <vector>

inline constexpr std::int64_t minute_micros = 60 * 1000000LL;

// The hub's in-game clock: the shared sky clock, read at the hub's own offset.
struct hub_clock {
    sky_timeline timeline;
    double utc_offset_hours = 0.0;
};

// Which in-game day of the hub's calendar a real moment (UTC microseconds) falls on.
inline std::int64_t hub_day_at(const hub_clock& clock, const std::int64_t micros) {
    return static_cast<std::int64_t>(std::floor(game_day_at(clock.timeline, micros / 1000) + clock.utc_offset_hours / 24.0));
}

// The first tee time at or after `now` (UTC microseconds).
inline std::int64_t next_tee_time(const std::int64_t now, const server_tuning& tuning) {
    const auto interval = tuning.tee_interval_minutes * minute_micros;
    return (now / interval + (now % interval != 0 ? 1 : 0)) * interval;
}

// Whether `at` is on a day the book reaches from `now`: never past the end of
// the hub's in-game day tee_book_days_ahead from now's.
inline bool within_tee_book(const std::int64_t at, const std::int64_t now, const server_tuning& tuning, const hub_clock& clock) {
    return hub_day_at(clock, at) <= hub_day_at(clock, now) + tuning.tee_book_days_ahead;
}

// Every bookable time from `now` to the booking horizon, in order.
inline std::vector<std::int64_t> tee_times(const std::int64_t now, const server_tuning& tuning, const hub_clock& clock) {
    std::vector<std::int64_t> times;
    for (auto at = next_tee_time(now, tuning); within_tee_book(at, now, tuning, clock); at += tuning.tee_interval_minutes * minute_micros) {
        times.push_back(at);
    }
    return times;
}
