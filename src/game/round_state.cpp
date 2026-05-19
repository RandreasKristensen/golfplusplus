#include "game/round_state.h"

#include <algorithm>

round_state start_course(const course_definition& course) {
    round_state round;
    round.course_id = course.id;
    round.current_hole_index = 0;
    round.strokes_per_hole.assign(static_cast<std::size_t>(std::max(0, course.hole_count)), 0);
    round.finished = round.strokes_per_hole.empty();
    return round;
}

bool complete_hole(round_state& round, const int strokes) {
    if (round.finished || round.current_hole_index >= round.strokes_per_hole.size()) {
        return false;
    }

    round.strokes_per_hole[round.current_hole_index] = std::max(0, strokes);
    return advance_to_next_hole(round);
}

bool advance_to_next_hole(round_state& round) {
    if (round.finished) {
        return false;
    }

    if (round.current_hole_index + 1 >= round.strokes_per_hole.size()) {
        round.finished = true;
        return false;
    }

    ++round.current_hole_index;
    return true;
}

int current_hole_score(const round_state& round) {
    if (round.current_hole_index >= round.strokes_per_hole.size()) {
        return 0;
    }
    return round.strokes_per_hole[round.current_hole_index];
}
