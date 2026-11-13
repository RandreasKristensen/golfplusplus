#include "check_runtime.h"
#include "game/hub_ball.h"
#include "game/mode_dispatch.h"
#include "game/server_errors.h"

bool check_practice(live_check& run) {
    auto& state = run.state(0);
    const auto progress = save_data_to_json(state.online.progress);
    net_command drop; drop.type = net_command_type::place_practice_ball; drop.placed = state.player.position;
    run.send(0, drop);
    if (!run.require("practice drop adopts a hole-free authoritative ball", run.until([&] {
        const auto found = state.online.balls.find(state.online.account_id);
        return found != state.online.balls.end() && found->second.kind == ball_kind::practice && found->second.hole == -1;
    }))) { return false; }
    run.send(0, drop);
    if (!run.refusal(0, reducer_place_practice_ball, error_ball_in_play)) { return false; }
    run.action(0, net_command_type::pick_up_ball);
    if (!run.require("practice pickup removes the whole ball row without progress", run.until([&] {
        return !state.online.balls.count(state.online.account_id);
    }) && save_data_to_json(state.online.progress) == progress)) { return false; }
    drop.placed += glm::vec3(100, 0, 100); run.send(0, drop);
    return run.refusal(0, reducer_place_practice_ball, error_too_far);
}
