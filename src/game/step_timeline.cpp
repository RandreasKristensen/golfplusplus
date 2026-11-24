#include "game/step_timeline.h"

#include <cmath>

namespace {
std::int64_t milliseconds(const float seconds) {
    return std::llround(static_cast<double>(seconds) * 1000.0);
}
}

std::int64_t step_time_ms(const std::int64_t first_time_ms, const std::uint64_t index) {
    // index * 25/3 ms, rounded to the nearest whole millisecond.
    return first_time_ms + static_cast<std::int64_t>((index * 25 + 1) / 3);
}

timeline_result place_batch(const std::optional<timeline_anchor>& previous, const timed_batch& batch, const std::int64_t now_ms,
                            const server_tuning& tuning) {
    timeline_result result;
    if (previous) {
        result.anchor = *previous;
    }
    if (batch.steps == 0) {
        result.repeated = true;
        return result;
    }
    const std::uint64_t last_index = batch.steps - 1u;
    const std::int64_t last_time = step_time_ms(batch.first_time_ms, last_index);
    if (last_time > now_ms + milliseconds(tuning.timing_slack_seconds)) {
        result.verdict = timeline_verdict::future;
        return result;
    }
    const std::uint64_t last_step = batch.first_step + last_index;
    if (previous) {
        if (last_step <= previous->step) {
            result.repeated = true;
            return result;
        }
        const std::uint64_t new_index = previous->step >= batch.first_step ? previous->step + 1 - batch.first_step : 0;
        const std::uint64_t gap = batch.first_step + new_index - previous->step;
        // The steps between them, each at least its length rounded down, less
        // a millisecond for the client's rounding of its own times.
        const std::int64_t earliest = previous->time_ms + static_cast<std::int64_t>(gap * 25 / 3) - 1;
        if (step_time_ms(batch.first_time_ms, new_index) < earliest) {
            result.verdict = timeline_verdict::overlap;
            return result;
        }
    }
    result.late = last_time < now_ms - milliseconds(tuning.contact_rewind_seconds);
    result.anchor = timeline_anchor{last_step, last_time};
    return result;
}
