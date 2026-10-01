#include "game/scorecard.h"

#include "game/game_state.h"
#include "game/text_ids.h"

#include <cstdlib>

std::string format_relative_score(const string_table& strings, const int relative_score) {
    if (relative_score == 0) {
        return lookup_text(strings, text_scorecard_even);
    }
    const std::string strokes = std::to_string(std::abs(relative_score));
    return format_text(strings, relative_score > 0 ? text_scorecard_over : text_scorecard_under, {{"strokes", strokes}});
}

scorecard_data build_scorecard_data(const game_state& state, const string_table& strings) {
    scorecard_data data;
    data.course_name = state.course.name;
    data.current_hole_index = state.round.current_hole_index;
    data.finished = round_finished(state.round);

    for (std::size_t i = 0; i < state.course_holes.size(); ++i) {
        const hole_data& hole = state.course_holes[i];
        scorecard_row row;
        row.hole_number = static_cast<int>(i) + 1;
        row.hole_name = hole.name;
        row.par = hole.par;
        row.played = hole_played(state.round, i);
        if (row.played) {
            row.strokes = *state.round.strokes[i];
            row.relative_label = format_relative_score(strings, row.strokes - row.par);
            data.total_par += row.par;
            data.total_strokes += row.strokes;
        }
        data.rows.push_back(row);
    }

    data.total_relative_label = format_relative_score(strings, data.total_strokes - data.total_par);
    return data;
}
