#include "game/motion_sync.h"

#include "game/game_state.h"
#include "game/mode_dispatch.h"
#include "physics/vector_math.h"

#include <cmath>
#include <utility>

namespace {
int direction(const float value, const float still) {
    return value > still ? 1 : (value < -still ? -1 : 0);
}

bool moving(const net_motion& motion) {
    return direction(motion.speed, still_speed) != 0 || direction(motion.turn_rate, still_turn_rate) != 0;
}

bool same_intent(const net_motion& a, const net_motion& b) {
    return a.zone == b.zone && a.mode == b.mode && a.on_road == b.on_road &&
        direction(a.speed, still_speed) == direction(b.speed, still_speed) &&
        direction(a.turn_rate, still_turn_rate) == direction(b.turn_rate, still_turn_rate);
}

motion_mode current_motion_mode(const game_state& state) {
    switch (state.mode) {
    case game_mode::aiming:
        return motion_mode::aim;
    case game_mode::addressing:
        return state.swing.phase == swing_phase::timing ? motion_mode::swing : motion_mode::address;
    case game_mode::following_shot:
        return motion_mode::idle;
    case game_mode::walking:
        break;
    }
    if (state.cart.active) {
        return state.cart.drift_timer > 0.0f ? motion_mode::drift : motion_mode::cart;
    }
    const bool walking = direction(state.player.speed, still_speed) != 0 ||
        direction(state.player.turn_rate, still_turn_rate) != 0;
    return walking ? motion_mode::walk : motion_mode::idle;
}
}

bool should_send_motion(const motion_sync_state& sync, const net_motion& now, const net_tuning& tuning) {
    if (!sync.last_sent) {
        return true;
    }
    if (sync.since_sent < tuning.motion_min_interval_seconds) {
        return false;
    }
    const net_motion& last = *sync.last_sent;
    if (!same_intent(last, now)) {
        return true;
    }
    if (moving(now) && sync.since_sent >= tuning.motion_heartbeat_seconds) {
        return true;
    }
    return horizontal_distance(extrapolate_motion(last, sync.since_sent), now.position) > tuning.motion_correction_distance;
}

net_motion current_motion(const game_state& state) {
    net_motion motion;
    motion.zone = local_zone(state);
    motion.mode = current_motion_mode(state);
    motion.position = state.player.position;
    motion.yaw = state.player.yaw;
    motion.speed = state.player.speed;
    motion.turn_rate = state.player.turn_rate;
    motion.on_road = state.cart.active && cart_on_road(state);
    return motion;
}

namespace {
void push_motion(game_state& state, const net_motion& now) {
    net_command command;
    command.type = net_command_type::motion;
    command.motion = now;
    state.net_commands.push_back(std::move(command));
    state.motion_sync = motion_sync_state{now, 0.0f};
}

// A finished round stays on its last hole here while the server has the
// player in the hub already: nothing is sent for it, from the frame it ends.
bool sends_motion(const game_state& state) {
    return is_online(state) && !round_finished(state.round);
}
}

void sync_motion(game_state& state, const float dt) {
    if (!sends_motion(state)) {
        return;
    }
    state.motion_sync.since_sent += dt;
    const net_motion now = current_motion(state);
    // Nor while my ball blends to where the server put it: the server may
    // have finished the hole already.
    if (shot_playing(state) || state.ball_correction || !should_send_motion(state.motion_sync, now, state.tuning.net)) {
        return;
    }
    push_motion(state, now);
}

void send_motion_now(game_state& state) {
    if (sends_motion(state)) {
        push_motion(state, current_motion(state));
    }
}
