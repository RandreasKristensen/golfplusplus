#include "game/my_balls.h"
#include "game/game_state.h"
#include "game/golf_bag.h"
#include "game/mode_dispatch.h"
#include "game/motion_sync.h"
#include "game/server_errors.h"
#include "game/text_ids.h"
#include <algorithm>

namespace {
// My ball in play is put away: nothing it was doing goes on.
void settle_ball_in_play(game_state& state) {
    state.shot.reset(); state.address = address_state{}; state.flight_path_points.clear();
    state.online_hole = online_hole_state{}; state.ball_correction.reset(); state.give_up_prompt.reset();
    state.mode = game_mode::walking;
}

const spare_ball* find_spare(const std::vector<spare_ball>& spares, const std::uint64_t id) {
    const auto found = std::find_if(spares.begin(), spares.end(), [id](const spare_ball& spare) { return spare.id == id; });
    return found != spares.end() ? &*found : nullptr;
}

bool busy_with_rows(const game_state& state) {
    return (is_online(state) && !state.online.room_ready) || state.hub_flow.ball_request;
}

// Online: the one request waiting on its complete rows.
void ask_server(game_state& state, const net_command_type type, const std::uint64_t id) {
    state.hub_flow.ball_request = type;
    state.hub_flow.ball_updates = state.online.ball_updates;
    state.hub_flow.ball_target = id;
    net_command command; command.type = type; command.ball_id = id; command.placed = state.player.position;
    request_tee_action(state, command);
}
}

std::vector<spare_ball> my_spare_balls(const game_state& state) {
    if (!is_online(state)) { return state.spare_balls; }
    std::vector<spare_ball> mine;
    for (const auto& [id, spare] : state.online.spare_balls) {
        if (spare.account == state.online.account_id) { mine.push_back(spare); }
    }
    std::sort(mine.begin(), mine.end(), [](const spare_ball& a, const spare_ball& b) {
        return a.placed_at != b.placed_at ? a.placed_at < b.placed_at : a.id < b.id;
    });
    return mine;
}

void put_new_ball_in_play(game_state& state, my_ball fresh) {
    std::optional<spare_ball> demoted;
    if (state.ball && state.ball->kind == ball_kind::practice && !state.shot) {
        demoted = spare_ball{0, 0, state.ball->position, state.ball->place == ball_place::in_cup, state.ball->placed_at};
    }
    fresh.placed_at = ++state.balls_placed;
    std::vector<spare_ball> kept = spares_after_new_ball(state.spare_balls, demoted, state.place.rules.balls_out);
    for (spare_ball& spare : kept) {
        if (spare.id == 0) { spare.id = ++state.balls_placed; }
    }
    state.spare_balls = std::move(kept);
    state.ball = fresh;
    settle_ball_in_play(state);
}

void request_practice_drop(game_state& state) {
    if (busy_with_rows(state)) { return; }
    const auto position = practice_drop_position(state.area, state.place.rules, state.player.position, state.player.position, state.tuning);
    if (!position) { state.notice = game_notice{online_error_text_key(error_not_available), 0.0f, message_severity::error}; return; }
    if (is_online(state)) {
        ask_server(state, net_command_type::place_practice_ball, 0);
        return;
    }
    put_new_ball_in_play(state, my_ball{std::nullopt, *position, 0, ball_place::at_rest, ball_kind::practice});
}

bool pick_up_practice_ball(game_state& state) {
    if (busy_with_rows(state) || !state.ball || !practice_pickup_in_reach(state.ball->kind, !state.shot,
        on_foot(state), state.player.position, state.ball->position, state.tuning)) { return false; }
    if (is_online(state)) {
        ask_server(state, net_command_type::pick_up_ball, 0);
        return true;
    }
    state.ball.reset();
    settle_ball_in_play(state);
    return true;
}

bool pick_up_spare_ball(game_state& state, const std::uint64_t id) {
    const std::vector<spare_ball> mine = my_spare_balls(state);
    const spare_ball* spare = find_spare(mine, id);
    if (busy_with_rows(state) || spare == nullptr || !practice_pickup_in_reach(ball_kind::practice, true, on_foot(state),
        state.player.position, spare->position, state.tuning)) { return false; }
    if (is_online(state)) {
        ask_server(state, net_command_type::pick_up_ball, id);
        return true;
    }
    state.spare_balls.erase(std::remove_if(state.spare_balls.begin(), state.spare_balls.end(),
        [id](const spare_ball& other) { return other.id == id; }), state.spare_balls.end());
    return true;
}

bool play_spare_ball(game_state& state, const std::uint64_t id) {
    const std::vector<spare_ball> mine = my_spare_balls(state);
    const spare_ball* spare = find_spare(mine, id);
    if (busy_with_rows(state) || spare == nullptr) { return false; }
    const std::optional<ball_in_play_state> playing = state.ball
        ? std::optional<ball_in_play_state>(ball_in_play_state{state.ball->kind, !state.shot}) : std::nullopt;
    if (!play_spare_error(playing, on_foot(state), state.player.position, *spare, state.tuning).empty()) { return false; }
    if (is_online(state)) {
        send_motion_now(state);
        ask_server(state, net_command_type::play_ball, id);
        return true;
    }
    const spare_ball chosen = *spare;
    std::vector<spare_ball>& spares = state.spare_balls;
    spares.erase(std::remove_if(spares.begin(), spares.end(), [id](const spare_ball& other) { return other.id == id; }), spares.end());
    if (state.ball) {
        spares.push_back(spare_ball{static_cast<std::uint64_t>(++state.balls_placed), 0, state.ball->position, state.ball->place == ball_place::in_cup,
            state.ball->placed_at});
    }
    state.ball = my_ball{std::nullopt, chosen.position, 0, ball_place::at_rest, ball_kind::practice, std::nullopt, chosen.placed_at};
    settle_ball_in_play(state);
    return true;
}
