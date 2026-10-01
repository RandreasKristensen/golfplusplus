#pragma once

// The scorecard rows for the course being played (hold Tab, and the results
// screen after the last hole). Built from round_state and the course holes.

#include "game/string_table.h"

#include <cstddef>
#include <string>
#include <vector>

struct game_state;

struct scorecard_row {
    int hole_number = 0;  // 1-based
    std::string hole_name;
    int par = 0;
    bool played = false;
    int strokes = 0;  // 0 until played
    std::string relative_label;  // empty until played
};

struct scorecard_data {
    std::string course_name;
    std::size_t current_hole_index = 0;
    std::vector<scorecard_row> rows;
    // Totals over played holes only.
    int total_par = 0;
    int total_strokes = 0;
    std::string total_relative_label;
    bool finished = false;
};

// "EVEN", "+2", "-1": text from the string table.
std::string format_relative_score(const string_table& strings, int relative_score);
scorecard_data build_scorecard_data(const game_state& state, const string_table& strings);
