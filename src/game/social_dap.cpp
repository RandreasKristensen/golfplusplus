#include "game/social_dap.h"
#include "game/dap_rules.h"
#include "game/mode_dispatch.h"
#include "game/motion_sync.h"
const room_dap* active_dap(const game_state& state, const std::uint64_t account) {
    if (!is_online(state) || !state.online.room_ready || !state.hub_flow.travel_course.empty()) { return nullptr; }
    for (const auto& [id, row] : state.online.daps) {
        if ((row.first == account || row.second == account) && state.online.players.count(row.first) &&
            state.online.players.count(row.second) && dap_running(row.started_at, state.online.server_now, state.tuning.dap)) { return &row; }
    }
    return nullptr;
}
bool dap_available(const game_state& state, const std::uint64_t target) {
    if (!is_online(state) || !state.online.room_ready || !state.online.room || state.mode != game_mode::walking ||
        !on_foot(state) || state.shot || state.handling || state.picker || state.smoking || state.drinking ||
        !state.player.vertical.grounded || state.crouch > 0.0f || state.dap_pending ||
        state.hub_flow.screen != hub_screen::none || state.hub_flow.ball_request || !state.hub_flow.travel_course.empty() ||
        active_dap(state, state.online.account_id) || active_dap(state, target)) { return false; }
    const auto other = state.online.players.find(target);
    if (other == state.online.players.end() || !other->second.motion) { return false; }
    const auto& motion = *other->second.motion;
    return check_dap({state.online.account_id, state.online.room->room_id, state.player.position, motion_mode::walk, false},
        {target, state.online.room->room_id, motion.position, motion.mode, motion.crouched}, state.tuning).empty();
}
void request_dap(game_state& state, const std::uint64_t target) {
    if (!dap_available(state, target)) { return; }
    send_motion_now(state);
    net_command command;
    command.type = net_command_type::dap; command.target_account = target; command.room_id = state.online.room->room_id;
    state.net_commands.push_back(command);
    state.dap_pending = target;
    state.dap_requested_at = state.online.server_now;
}
void reconcile_dap(game_state& state) {
    if (!state.dap_pending) { return; }
    if (!state.online.room_ready || !state.online.players.count(*state.dap_pending) ||
        !state.hub_flow.travel_course.empty() || active_dap(state, state.online.account_id) ||
        state.online.server_now - state.dap_requested_at >= static_cast<std::int64_t>(state.tuning.net.max_unacked_seconds * 1000000.0f)) {
        state.dap_pending.reset();
    }
}
bool dap_holds_movement(const game_state& state) {
    return state.dap_pending.has_value() || active_dap(state, state.online.account_id);
}
