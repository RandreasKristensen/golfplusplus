#include "game/boarding.h"

#include "game/mode_dispatch.h"
#include "game/tee_times.h"
#include "game/world_sounds.h"

#include <algorithm>
#include <set>

namespace {
// The times of the departures this player can hear: my booking's, and the board's.
std::set<std::int64_t> heard_departures(const game_state& state) {
    std::set<std::int64_t> times;
    const auto& booking = active_booking(state);
    if (booking && booking->scheduled_at != 0 && booking->started_at == 0 && !booking->completed) { times.insert(booking->scheduled_at); }
    for (const departure_info& departure : state.online.departures) { times.insert(departure.scheduled_at); }
    return times;
}
}

boarding_phase boarding_phase_at(const std::int64_t scheduled, const std::int64_t now, const server_tuning& tuning) {
    if (now >= scheduled) { return boarding_phase::gone; }
    if (now >= scheduled - static_cast<std::int64_t>(tuning.last_call_seconds * 1e6f)) { return boarding_phase::last_call; }
    return now >= scheduled - tuning.bus_early_minutes * minute_micros ? boarding_phase::boarding : boarding_phase::waiting;
}

bool bus_doors_open(const game_state& state) {
    if (state.kind != place_kind::hub) { return false; }
    if (!is_online(state)) { return active_booking(state).has_value(); }
    if (state.online.server_now == 0) { return false; }
    const auto times = heard_departures(state);
    return std::any_of(times.begin(), times.end(), [&](const std::int64_t at) {
        const boarding_phase phase = boarding_phase_at(at, state.online.server_now, state.tuning.server);
        return phase == boarding_phase::boarding || phase == boarding_phase::last_call;
    });
}

void update_boarding(game_state& state) {
    auto& seen = state.hub_flow.boarding_seen;
    if (state.kind != place_kind::hub || !is_online(state) || state.online.server_now == 0) { seen.clear(); return; }
    const auto times = heard_departures(state);
    for (auto it = seen.begin(); it != seen.end();) { it = times.count(it->first) != 0 ? std::next(it) : seen.erase(it); }
    for (const std::int64_t at : times) {
        const boarding_phase phase = boarding_phase_at(at, state.online.server_now, state.tuning.server);
        const auto before = seen.find(at);
        if (before == seen.end()) { seen[at] = phase; continue; }
        if (phase > before->second && state.area.hub_sounds) {
            if (before->second < boarding_phase::boarding && phase >= boarding_phase::boarding && phase != boarding_phase::gone) {
                push_audio_event(state, audio_event_type::boarding_call, state.area.hub_sounds->announcer);
            }
            if (before->second < boarding_phase::last_call && phase >= boarding_phase::last_call && phase != boarding_phase::gone) {
                push_audio_event(state, audio_event_type::bus_horn, state.area.hub_sounds->bus_horn);
            }
        }
        before->second = std::max(before->second, phase);
    }
}
