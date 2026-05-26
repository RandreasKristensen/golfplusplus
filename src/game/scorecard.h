#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct game_state;

struct scorecard_row {
    int hole_number = 0;
    std::string hole_name;
    int par = 0;
    int strokes = 0;
    bool played = false;
    int relative_score = 0;
    std::string relative_label;
};

struct scorecard_data {
    std::string course_name;
    std::size_t current_hole_index = 0;
    std::vector<scorecard_row> rows;
    int total_par = 0;
    int total_strokes = 0;
    int total_relative_score = 0;
    std::string total_relative_label;
    bool finished = false;
};

std::string format_relative_score(int relative_score);
scorecard_data build_scorecard_data(const game_state& state);
