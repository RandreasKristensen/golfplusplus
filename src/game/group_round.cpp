#include "game/group_round.h"

#include "game/game_state.h"
#include "game/mode_dispatch.h"
#include "game/remote_players.h"

#include <algorithm>

namespace {
std::vector<int>& seen_strokes(game_state& state, const std::uint64_t account) {
    std::vector<int>& strokes = state.group_strokes[account];
    strokes.resize(state.course_holes.size(), 0);
    return strokes;
}
}

void note_round_strokes(game_state& state) {
    for (const auto& [account, player] : state.online.players) {
        for (std::size_t i = 0; i < player.round_strokes.size() && i < state.course_holes.size(); ++i) {
            if (player.round_strokes[i] > 0) {
                seen_strokes(state, account)[i] = player.round_strokes[i];
            }
        }
    }
}

void note_holed_shot(game_state& state, const room_shot& shot) {
    if (shot.holed && shot.zone >= 0 && static_cast<std::size_t>(shot.zone) < state.course_holes.size()) {
        seen_strokes(state, shot.account_id)[static_cast<std::size_t>(shot.zone)] = shot.stroke;
    }
}

std::vector<int> member_round_strokes(const game_state& state, const std::uint64_t account) {
    std::vector<int> strokes(state.course_holes.size(), 0);
    if (account == state.online.account_id) {
        for (std::size_t i = 0; i < strokes.size(); ++i) {
            strokes[i] = hole_played(state.round, i) ? *state.round.strokes[i] : 0;
        }
        return strokes;
    }
    const auto seen = state.group_strokes.find(account);
    if (seen != state.group_strokes.end()) {
        for (std::size_t i = 0; i < strokes.size() && i < seen->second.size(); ++i) {
            strokes[i] = seen->second[i];
        }
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
    if (!is_online(state) || !round_finished(state.round) || !state.hole) {
        return false;
    }
    const room_player* me = my_room_player(state);
    if (me == nullptr || me->group_id == 0) {
        return false;
    }
    // A finished round stays on the hole it finished on.
    const int last_hole = static_cast<int>(state.hole->index);
    const auto shooting = [&state](const std::uint64_t account) {
        const auto playing = state.remote_shots.find(account);
        if (playing != state.remote_shots.end() && remote_shot_playing(playing->second)) {
            return true;
        }
        // A shot arrived with the zone it ended in, not played yet.
        return std::any_of(state.online.shots.begin(), state.online.shots.end(),
                           [account](const room_shot& shot) { return shot.account_id == account; });
    };
    for (const auto& [account, player] : state.online.players) {
        if (account != state.online.account_id && player.group_id == me->group_id &&
            (player.zone == last_hole || shooting(account))) {
            return true;
        }
    }
    return false;
}

bool round_results_shown(const game_state& state) {
    return round_finished(state.round) && !waiting_for_group(state);
}
