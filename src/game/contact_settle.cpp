#include "game/contact_settle.h"

#include <algorithm>
#include <cmath>

namespace {
std::int64_t milliseconds(const float seconds) {
    return std::llround(static_cast<double>(seconds) * 1000.0);
}
}

std::int64_t settled_until_ms(const std::vector<std::int64_t>& last_stepped_ms, const std::int64_t now_ms,
                              const server_tuning& tuning) {
    const std::int64_t window_start = now_ms - milliseconds(tuning.contact_rewind_seconds);
    if (last_stepped_ms.empty()) {
        return window_start;
    }
    const std::int64_t earliest = std::min(now_ms, *std::min_element(last_stepped_ms.begin(), last_stepped_ms.end()));
    return std::max(earliest, window_start);
}

bool contact_settled(const ball_flight& flight, const flight_contact& contact, const std::int64_t settled_until_ms) {
    return step_clock_us(flight.launch_ms, contact.step + 1) <= settled_until_ms * 1000;
}

std::int64_t settle_due_ms(const ball_flight& flight, const flight_contact& contact, const server_tuning& tuning) {
    const std::int64_t micros = step_clock_us(flight.launch_ms, contact.step + 1);
    return (micros + 999) / 1000 + milliseconds(tuning.contact_rewind_seconds);
}

std::vector<flight_contact> pending_with(const std::vector<flight_contact>& pending, const flight_contact& found) {
    const bool earlier = std::all_of(pending.begin(), pending.end(), [&found](const flight_contact& other) { return found.step < other.step; });
    return earlier ? std::vector<flight_contact>{found} : pending;
}
