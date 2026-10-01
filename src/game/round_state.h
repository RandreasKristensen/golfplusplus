#pragma once

#include <cstddef>
#include <optional>
#include <vector>

// Strokes per hole for the course being played. In a hub, holes can be played
// in any order; the round is finished once every hole has a score.
struct round_state {
    std::vector<std::optional<int>> strokes;  // nullopt until the hole is completed
    std::size_t current_hole_index = 0;       // the hole being played, or the next to play
};

round_state start_round(std::size_t hole_count);
// Records the score and moves current_hole_index to the next unplayed hole
// (searching forward, wrapping around).
round_state complete_hole(const round_state& round, std::size_t hole_index, int strokes);
bool hole_played(const round_state& round, std::size_t hole_index);
bool round_finished(const round_state& round);
