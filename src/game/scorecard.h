#pragma once

// The scorecard for the course being played (hold Tab, and the results
// screen after the last hole): every hole's par and a row of strokes per
// player. Built from round_state and the course holes; online, in a group,
// a row per member as this round has seen them (game/group_round.h).

#include "game/string_table.h"

#include <cstddef>
#include <string>
#include <vector>

struct game_state;

struct scorecard_player {
    std::string name;
    std::vector<int> strokes;  // per hole, 0 until played
    int total_strokes = 0;
    std::string relative_label;  // against the par of the holes played
    bool me = false;  // marked in a group only
};

struct scorecard_data {
    std::string course_name;
    std::size_t current_hole_index = 0;
    std::vector<int> pars;  // per hole
    std::vector<scorecard_player> players;
    bool finished = false;
    // Online a finished round goes on to the next one in the same room;
    // offline it goes back to the menu.
    bool next_round = false;
};

// "EVEN", "+2", "-1": text from the string table.
std::string format_relative_score(const string_table& strings, int relative_score);
// A row for `strokes` (per hole, 0 until played) with its totals.
scorecard_player make_scorecard_player(const string_table& strings,
                                       const std::string& name,
                                       const std::vector<int>& strokes,
                                       const std::vector<int>& pars);
// Offline or outside a group one row, mine; in a group one per member, me
// included.
scorecard_data build_scorecard_data(const game_state& state, const string_table& strings);
