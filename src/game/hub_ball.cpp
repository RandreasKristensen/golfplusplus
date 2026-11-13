#include "game/hub_ball.h"
#include "game/game_state.h"
#include "game/golf_bag.h"
#include "game/mode_dispatch.h"
#include "game/motion_sync.h"
#include "game/server_errors.h"
#include "game/text_ids.h"

void request_practice_drop(game_state& state) {
    if ((is_online(state) && !state.online.room_ready) || state.hub_flow.ball_request) { return; }
    const auto position = practice_drop_position(state.area, state.kind, state.player.position, state.player.position, state.tuning);
    if (!position) { state.notice = game_notice{online_error_text_key(error_not_available), 0}; return; }
    if (is_online(state)) {
        state.hub_flow.ball_request = net_command_type::place_practice_ball;
        state.hub_flow.ball_updates = state.online.ball_updates;
        net_command command; command.type = net_command_type::place_practice_ball; command.placed = state.player.position;
        request_tee_action(state, command);
        return;
    }
    state.ball = my_ball{std::nullopt, *position, 0, ball_place::at_rest, ball_kind::practice};
    state.shot.reset(); state.address = address_state{}; state.flight_path_points.clear();
    state.online_hole = online_hole_state{}; state.ball_correction.reset(); state.give_up_prompt.reset();
    state.mode = game_mode::walking;
}

bool pick_up_practice_ball(game_state& state) {
    if ((is_online(state) && !state.online.room_ready) || !state.ball || state.hub_flow.ball_request || !practice_pickup_in_reach(state.ball->kind, !state.shot,
        on_foot(state), state.player.position, state.ball->position, state.tuning)) { return false; }
    if (is_online(state)) {
        state.hub_flow.ball_request = net_command_type::pick_up_ball;
        state.hub_flow.ball_updates = state.online.ball_updates;
        net_command command; command.type = net_command_type::pick_up_ball;
        request_tee_action(state, command);
        return true;
    }
    state.ball.reset(); state.shot.reset(); state.address = address_state{}; state.mode = game_mode::walking;
    state.flight_path_points.clear(); state.online_hole = online_hole_state{};
    state.ball_correction.reset(); state.give_up_prompt.reset();
    return true;
}
