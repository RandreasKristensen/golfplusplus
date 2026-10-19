#include "game/scorecard.h"

#include "game/game_state.h"
#include "game/group_round.h"
#include "game/mode_dispatch.h"
#include "game/text_ids.h"

#include <cstdlib>

std::string format_relative_score(const string_table& strings, const int relative_score) {
    if (relative_score == 0) {
        return lookup_text(strings, text_scorecard_even);
    }
    const std::string strokes = std::to_string(std::abs(relative_score));
    return format_text(strings, relative_score > 0 ? text_scorecard_over : text_scorecard_under, {{"strokes", strokes}});
}

scorecard_player make_scorecard_player(const string_table& strings,
                                       const std::string& name,
                                       const std::vector<int>& strokes,
                                       const std::vector<int>& pars) {
    scorecard_player player;
    player.name = name;
    player.strokes = strokes;
    player.strokes.resize(pars.size(), 0);
    int par = 0;
    for (std::size_t i = 0; i < pars.size(); ++i) {
        if (player.strokes[i] > 0) {
            player.total_strokes += player.strokes[i];
            par += pars[i];
        }
    }
    player.relative_label = format_relative_score(strings, player.total_strokes - par);
    return player;
}

scorecard_data build_scorecard_data(const game_state& state, const string_table& strings) {
    scorecard_data data;
    data.course_name = state.course.name;
    data.current_hole_index = state.round.current_hole_index;
    data.finished = round_finished(state.round);
    data.next_round = is_online(state);
    for (const hole_data& hole : state.course_holes) {
        data.pars.push_back(hole.par);
    }

    const room_player* me = is_online(state) ? my_room_player(state) : nullptr;
    if (me != nullptr && me->group_id != 0) {
        for (const auto& [account, player] : state.online.players) {
            if (player.group_id == me->group_id) {
                data.players.push_back(make_scorecard_player(strings, player.name, member_round_strokes(state, account), data.pars));
                data.players.back().me = account == state.online.account_id;
            }
        }
        return data;
    }

    std::vector<int> strokes(state.course_holes.size(), 0);
    for (std::size_t i = 0; i < strokes.size(); ++i) {
        if (hole_played(state.round, i)) {
            strokes[i] = *state.round.strokes[i];
        }
    }
    data.players.push_back(make_scorecard_player(strings, lookup_text(strings, text_scorecard_header_score), strokes, data.pars));
    return data;
}
