#pragma once

#include "game/course_definition.h"

#include <cstddef>
#include <string>
#include <vector>

struct round_state {
    std::string course_id;
    std::size_t current_hole_index = 0;
    std::vector<int> strokes_per_hole;
    bool finished = false;
};

round_state start_course(const course_definition& course);
bool complete_hole(round_state& round, int strokes);
bool advance_to_next_hole(round_state& round);
int current_hole_score(const round_state& round);
