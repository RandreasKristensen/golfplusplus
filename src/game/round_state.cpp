#include "game/round_state.h"

#include <algorithm>

round_state start_round(const std::size_t hole_count) {
    round_state round;
    round.strokes.assign(hole_count, std::nullopt);
    return round;
}

round_state complete_hole(const round_state& round, const std::size_t hole_index, const int strokes) {
    round_state next = round;
    if (hole_index >= next.strokes.size()) {
        return next;
    }
    next.strokes[hole_index] = std::max(0, strokes);
    for (std::size_t step = 1; step <= next.strokes.size(); ++step) {
        const std::size_t candidate = (hole_index + step) % next.strokes.size();
        if (!next.strokes[candidate]) {
            next.current_hole_index = candidate;
            break;
        }
    }
    return next;
}

bool hole_played(const round_state& round, const std::size_t hole_index) {
    return hole_index < round.strokes.size() && round.strokes[hole_index].has_value();
}

bool round_finished(const round_state& round) {
    return !round.strokes.empty() &&
        std::all_of(round.strokes.begin(), round.strokes.end(), [](const std::optional<int>& s) { return s.has_value(); });
}
