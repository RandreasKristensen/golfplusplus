#include "game/bag_sync.h"

#include "game/game_state.h"
#include "game/golf_bag.h"
#include "game/mode_dispatch.h"

namespace {
const room_bag* my_bag_row(const game_state& state) {
    const auto row = state.online.bags.find(state.online.account_id);
    return row != state.online.bags.end() ? &row->second : nullptr;
}

bool same_row(const room_bag& a, const room_bag& b) {
    return same_place(a.place, b.place) && a.club_in_hand == b.club_in_hand;
}

void hold_club(game_state& state, const std::string& club_id) {
    for (std::size_t i = 0; i < state.clubs.size(); ++i) {
        if (state.clubs[i].id == club_id) {
            state.selected_club = i;
        }
    }
}

bag_expectation& expectation(game_state& state) {
    if (!state.bag_expected) {
        state.bag_expected = bag_expectation{};
    }
    return *state.bag_expected;
}
}

void expect_bag_move(game_state& state) {
    bag_expectation& expected = expectation(state);
    if (!expected.place) {
        expected.place_seconds = 0.0f;
    }
    expected.place = state.bag;
}

void expect_club_taken(game_state& state, const std::string& club_id) {
    bag_expectation& expected = expectation(state);
    if (!expected.club) {
        expected.club_seconds = 0.0f;
    }
    expected.club = club_id;
}

void adopt_bag_row(game_state& state) {
    const room_bag* row = my_bag_row(state);
    if (row == nullptr) {
        return;
    }
    state.bag = row->place;
    hold_club(state, row->club_in_hand);
    state.picker.reset();
    state.handling.reset();
    state.bag_row_seen = *row;
    state.bag_expected.reset();
}

void reconcile_bag_row(game_state& state, const float dt) {
    const room_bag* row = is_online(state) ? my_bag_row(state) : nullptr;
    if (row == nullptr) {
        return;
    }
    if (!state.bag_row_seen && !state.bag_expected) {
        adopt_bag_row(state);
        return;
    }
    const bool changed = !state.bag_row_seen || !same_row(*state.bag_row_seen, *row);
    bag_expectation waiting = state.bag_expected.value_or(bag_expectation{});
    if (changed) {
        state.bag_row_seen = *row;
        if (waiting.place) {
            if (same_place(*waiting.place, row->place)) { waiting.place.reset(); }
        } else if (!same_place(state.bag, row->place)) {
            state.bag = row->place;
            if (state.handling && state.handling->kind == handling_kind::bag) { state.handling.reset(); }
        }
        if (waiting.club) {
            if (*waiting.club == row->club_in_hand) { waiting.club.reset(); }
        } else if (state.selected_club >= state.clubs.size() || state.clubs[state.selected_club].id != row->club_in_hand) {
            hold_club(state, row->club_in_hand);
            if (state.handling && state.handling->kind == handling_kind::club) { state.handling.reset(); }
        }
    }
    waiting.place_seconds += waiting.place ? dt : 0.0f;
    waiting.club_seconds += waiting.club ? dt : 0.0f;
    const float limit = state.tuning.net.max_unacked_seconds;
    if ((waiting.place && waiting.place_seconds > limit) || (waiting.club && waiting.club_seconds > limit)) {
        // Neither shown nor refused: lost, or the row has moved on. It wins.
        const glm::vec3 predicted = bag_position(state);
        adopt_bag_row(state);
        motion_sync_state& sync = state.motion_sync;
        ++sync.breaches;
        sync.reports.push_back(sync_breach{"bag_timeout", predicted, bag_position(state), sync.acknowledged, sync.last_refusal});
        return;
    }
    state.bag_expected = waiting.place || waiting.club ? std::optional<bag_expectation>(waiting) : std::nullopt;
}
