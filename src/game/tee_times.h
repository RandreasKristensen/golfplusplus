#pragma once

// The UTC tee times a course offers: one every tee_interval_minutes from the
// next whole interval, tee_book_days ahead. They are a function of the clock
// and the tuning alone, so the server keeps a tee_slot row only for a time
// someone has booked, and the tee book lists every other time itself.
#include "game/game_tuning.h"

#include <cstdint>
#include <vector>

inline constexpr std::int64_t minute_micros = 60 * 1000000LL;

// The first tee time at or after `now` (UTC microseconds).
inline std::int64_t next_tee_time(const std::int64_t now, const server_tuning& tuning) {
    const auto interval = tuning.tee_interval_minutes * minute_micros;
    return (now / interval + (now % interval != 0 ? 1 : 0)) * interval;
}

// Every bookable time from `now` to the booking horizon, in order.
inline std::vector<std::int64_t> tee_times(const std::int64_t now, const server_tuning& tuning) {
    std::vector<std::int64_t> times;
    const auto horizon = now + tuning.tee_book_days * 24 * 60 * minute_micros;
    for (auto at = next_tee_time(now, tuning); at <= horizon; at += tuning.tee_interval_minutes * minute_micros) {
        times.push_back(at);
    }
    return times;
}
