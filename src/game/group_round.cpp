#include "game/group_round.h"

#include "game/game_state.h"
#include "game/mode_dispatch.h"
#include "game/remote_players.h"

#include <algorithm>

std::vector<int> member_round_strokes(const game_state& state, const std::uint64_t account) {
    std::vector<int> strokes(state.course_holes.size(), 0);
    if (!state.round) { return strokes; }
    if (account == state.online.account_id) {
        for (std::size_t i = 0; i < strokes.size(); ++i) {
            strokes[i] = hole_played(*state.round, i) ? *state.round->strokes[i] : 0;
        }
        return strokes;
    }
    const auto player = state.online.players.find(account);
    if (player != state.online.players.end()) {
        const std::vector<int>& now = player->second.round_strokes;
        for (std::size_t i = 0; i < strokes.size() && i < now.size(); ++i) {
            if (now[i] > 0) {
                strokes[i] = now[i];
            }
        }
    }
    return strokes;
}

bool waiting_for_group(const game_state& state) {
    if (!is_online(state) || !(state.round && round_finished(*state.round)) || !state.ball) {
        return false;
    }
    const room_player* me = my_room_player(state);
    if (me == nullptr || me->group_id == 0) {
        return false;
    }
    // My ball waits in the cup of the hole the round finished on.
    const int last_hole = (state.ball->hole ? static_cast<int>(*state.ball->hole) : -1);
    const auto shooting = [&state](const std::uint64_t account) {
        const auto playing = state.remote_shots.find(account);
        if (playing != state.remote_shots.end() && remote_shot_playing(playing->second)) {
            return true;
        }
        // A shot arrived, not played yet.
        return std::any_of(state.online.shots.begin(), state.online.shots.end(),
                           [account](const room_shot& shot) { return shot.account_id == account; });
    };
    const auto playing_last_hole = [&state, last_hole](const std::uint64_t account) {
        const auto ball = state.online.balls.find(account);
        return ball != state.online.balls.end() && ball->second.hole == last_hole && !ball->second.in_cup;
    };
    for (const auto& [account, player] : state.online.players) {
        if (account != state.online.account_id && player.group_id == me->group_id &&
            (playing_last_hole(account) || shooting(account))) {
            return true;
        }
    }
    return false;
}

bool round_results_shown(const game_state& state) {
    return !state.results_dismissed && (state.round && round_finished(*state.round)) && !waiting_for_group(state);
}
