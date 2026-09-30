#include "game/scorecard.h"

#include "game/course_loader.h"
#include "game/game_state.h"
#include "game/hole_loader.h"
#include "game/text_ids.h"

#include <algorithm>
#include <cstdlib>
#include <optional>

namespace {
scorecard_row make_scorecard_row(const game_state& state, const string_table& strings, const std::size_t index) {
    scorecard_row row;
    row.hole_number = static_cast<int>(index) + 1;
    row.hole_name = format_text(strings, text_scorecard_hole_fallback, {{"hole", std::to_string(std::max(1, row.hole_number))}});
    row.par = 3;

    const std::optional<hole_data> hole = load_hole_from_file(course_hole_path(state.asset_root, state.active_course, index));
    if (hole) {
        if (!hole->name.empty()) {
            row.hole_name = hole->name;
        }
        row.par = std::max(1, hole->par);
    }

    if (index < state.round.strokes_per_hole.size()) {
        row.strokes = std::max(0, state.round.strokes_per_hole[index]);
    }

    const auto saved_score = state.save.hole_scores.find(static_cast<int>(index));
    row.played = state.round.finished || row.strokes > 0 || saved_score != state.save.hole_scores.end();
    if (saved_score != state.save.hole_scores.end()) {
        row.strokes = std::max(0, saved_score->second);
    }

    row.relative_score = row.strokes - row.par;
    row.relative_label = format_relative_score(strings, row.relative_score);
    return row;
}
}

std::string format_relative_score(const string_table& strings, const int relative_score) {
    if (relative_score == 0) {
        return lookup_text(strings, text_scorecard_even);
    }
    const std::string strokes = std::to_string(std::abs(relative_score));
    return format_text(strings, relative_score > 0 ? text_scorecard_over : text_scorecard_under, {{"strokes", strokes}});
}

scorecard_data build_scorecard_data(const game_state& state, const string_table& strings) {
    scorecard_data data;
    data.course_name = state.active_course.name.empty() ? state.active_course.id : state.active_course.name;
    data.current_hole_index = state.round.current_hole_index;
    data.finished = state.round.finished;

    const std::size_t hole_count = std::max(state.active_course.holes.size(), state.round.strokes_per_hole.size());
    data.rows.reserve(hole_count);
    for (std::size_t i = 0; i < hole_count; ++i) {
        scorecard_row row = make_scorecard_row(state, strings, i);
        const bool count_for_total = data.finished || row.played;
        if (count_for_total) {
            data.total_par += row.par;
            data.total_strokes += row.strokes;
        }
        data.rows.push_back(row);
    }

    data.total_relative_score = data.total_strokes - data.total_par;
    data.total_relative_label = format_relative_score(strings, data.total_relative_score);
    return data;
}
