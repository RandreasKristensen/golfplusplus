#include "game/bag_handling.h"

#include "game/game_state.h"
#include "physics/vector_math.h"

float handling_seconds(const bag_handling& handling, const player_tuning& tuning) {
    return handling.kind == handling_kind::club ? tuning.club_swap_seconds : tuning.bag_handle_seconds;
}

float handling_progress(const bag_handling& handling, const player_tuning& tuning) {
    const float seconds = handling_seconds(handling, tuning);
    return seconds > 0.0f ? clamp01(handling.elapsed / seconds) : 1.0f;
}

void start_bag_handling(game_state& state, const bag_place& from) {
    bag_handling handling;
    handling.kind = handling_kind::bag;
    handling.from = from;
    handling.to = state.bag;
    state.handling = handling;
}

void start_club_swap(game_state& state, const std::string& club_out) {
    bag_handling handling;
    handling.kind = handling_kind::club;
    handling.from = state.bag;
    handling.to = state.bag;
    handling.club_out = club_out;
    handling.club_in = state.clubs[state.selected_club].id;
    state.handling = handling;
}

bool handling_bag(const game_state& state) {
    return state.handling.has_value();
}

std::optional<bag_handling> handling_after(const bag_handling& handling, const player_tuning& tuning, const float dt) {
    bag_handling next = handling;
    next.elapsed += dt;
    return next.elapsed < handling_seconds(next, tuning) ? std::optional<bag_handling>(next) : std::nullopt;
}

void update_bag_handling(game_state& state, const float dt) {
    if (state.handling) {
        state.handling = handling_after(*state.handling, state.tuning.player, dt);
    }
}
