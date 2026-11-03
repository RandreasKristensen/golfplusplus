#include "game/motion_sync.h"

#include "game/game_state.h"
#include "game/green_read.h"
#include "game/group_round.h"
#include "game/mode_dispatch.h"
#include "game/ride_sync.h"
#include "physics/vector_math.h"

#include <cmath>
#include <utility>

namespace {
int direction(const float value, const float still) {
    return value > still ? 1 : (value < -still ? -1 : 0);
}

bool on_foot_mode(const motion_mode mode) {
    return mode == motion_mode::walk || mode == motion_mode::idle;
}

bool moving(const net_motion& motion) {
    return direction(motion.speed, still_speed) != 0 || direction(motion.turn_rate, still_turn_rate) != 0;
}

bool same_intent(const net_motion& a, const net_motion& b) {
    return a.mode == b.mode && a.on_road == b.on_road &&
        direction(a.speed, still_speed) == direction(b.speed, still_speed) &&
        direction(a.turn_rate, still_turn_rate) == direction(b.turn_rate, still_turn_rate) &&
        a.crouched == b.crouched && (!at_ball(a.mode) || a.setup == b.setup);
}

motion_mode current_motion_mode(const game_state& state) {
    switch (state.mode) {
    case game_mode::addressing:
        return state.address.swing_elapsed ? motion_mode::swing : motion_mode::address;
    case game_mode::following_shot:
        return motion_mode::idle;
    case game_mode::walking:
        break;
    }
    if (state.ride && !state.ride->step &&
        (!is_online(state) || state.ride->confirmed)) {
        return state.ride->owner == 0 ? motion_mode::cart : motion_mode::ride;
    }
    const bool walking = direction(state.player.speed, still_speed) != 0 ||
        direction(state.player.turn_rate, still_turn_rate) != 0;
    return walking ? motion_mode::walk : motion_mode::idle;
}
}

bool should_send_motion(const motion_sync_state& sync, const net_motion& now, const net_tuning& tuning) {
    const auto& latest = sync.queued ? sync.queued : sync.last_sent;
    const float since = sync.queued ? sync.since_queued : sync.since_sent;
    if (!latest) {
        return true;
    }
    if (since < tuning.motion_min_interval_seconds) {
        return false;
    }
    const net_motion& last = *latest;
    if (!same_intent(last, now)) {
        return true;
    }
    if ((moving(now) || now.mode == motion_mode::cart || !sync.history.empty()) && since >= tuning.motion_heartbeat_seconds) {
        return true;
    }
    const float yaw_off = wrap_angle(now.yaw - (last.yaw + last.turn_rate * since));
    return horizontal_distance(extrapolate_motion(last, since), now.position) > tuning.motion_correction_distance ||
        std::abs(yaw_off) > tuning.motion_correction_yaw;
}

net_motion current_motion(const game_state& state) {
    net_motion motion;
    motion.mode = current_motion_mode(state);
    motion.position = state.player.position;
    // Along the way I go, so others see me walk the way I step.
    motion.yaw = wrap_angle(state.player.yaw + state.player.move_angle);
    motion.speed = state.player.speed;
    motion.turn_rate = state.player.turn_rate;
    motion.on_road = driving_cart(state) && cart_on_road(state);
    if (at_ball(motion.mode)) {
        motion.setup = sent_setup_of(state.address.setup);
    }
    motion.crouched = on_foot_mode(motion.mode) && reading_green(state);
    return motion;
}

namespace {
void push_motion(game_state& state, net_motion now) {
    motion_sync_state& sync = state.motion_sync;
    net_command command;
    command.type = net_command_type::motion;
    command.runs = std::move(sync.unsent);
    command.first_step = sync.unsent_first;
    command.start = sync.unsent_start;
    command.last_step = sync.step;
    sync.unsent.clear();
    // Steps on foot leave me where the server's steps will: where I was put
    // since is not theirs (the server puts me there itself).
    const bool walked = !command.runs.empty() && command.runs.back().input.kind == move_kind::walk;
    if (walked && on_foot_mode(now.mode) && sync.stepped) {
        now.position = sync.stepped->position;
        now.yaw = sync.stepped->yaw;
        now.speed = sync.stepped->speed;
    }
    command.motion = now;
    if (now.mode == motion_mode::cart) {
        command.cart_prediction = state.cart;
    }
    state.net_commands.push_back(std::move(command));
    sync.queued = now;
    sync.since_queued = 0.0f;
}

// The player moves while a finished round's results wait for the group;
// once they show, nothing is sent.
bool sends_motion(const game_state& state) {
    return is_online(state) && (!round_finished(state.round) || waiting_for_group(state));
}

// Not while the camera follows my shot, nor while my ball blends to where
// the server put it: the server may have finished the hole already.
bool holds_motion(const game_state& state) {
    return (state.ride && state.ride->owner == 0 && (!state.ride->confirmed ||
            (state.ride->step && state.ride->step->getting_in))) ||
        state.mode == game_mode::following_shot || state.ball_correction;
}

// Where I was put since my last step without a step, added to the latest
// motion queued or sent, whose row may already show it.
void note_placement(game_state& state, const glm::vec3& placed) {
    for (auto command = state.net_commands.rbegin(); command != state.net_commands.rend(); ++command) {
        if (command->type == net_command_type::motion) {
            command->placed += placed;
            return;
        }
    }
    if (!state.motion_sync.history.empty()) {
        state.motion_sync.history.back().placed += placed;
    }
}
}

void sync_motion(game_state& state, const float dt) {
    if (!sends_motion(state)) {
        return;
    }
    state.motion_sync.since_sent += dt;
    state.motion_sync.since_queued += dt;
    const net_motion now = current_motion(state);
    sync_ride_motion(state, dt);
    if (holds_motion(state) || !should_send_motion(state.motion_sync, now, state.tuning.net)) {
        return;
    }
    push_motion(state, now);
}

void send_motion_now(game_state& state) {
    if (sends_motion(state)) {
        push_motion(state, current_motion(state));
    }
}

std::uint64_t record_step(game_state& state, const move_input& input) {
    motion_sync_state& sync = state.motion_sync;
    ++sync.step;
    if (!is_online(state)) {
        return sync.step;
    }
    const bool other_kind = !sync.unsent.empty() && sync.unsent.back().input.kind != input.kind;
    glm::vec3 placed(0.0f);
    if (input.kind == move_kind::walk && sync.stepped) {
        placed = state.player.position - sync.stepped->position;
    } else if (input.kind == move_kind::cart) {
        sync.stepped.reset();
    }
    const bool moved_since = placed != glm::vec3(0.0f);
    if ((other_kind || (moved_since && !sync.unsent.empty())) && sends_motion(state) && !holds_motion(state)) {
        // Numbered after the steps it sends.
        --sync.step;
        push_motion(state, current_motion(state));
        ++sync.step;
    }
    if (moved_since) {
        note_placement(state, placed);
        sync.stepped.reset();
    }
    if (sync.unsent.empty()) {
        sync.unsent_first = sync.step;
        sync.unsent_start = state.player.position;
    }
    append_step(sync.unsent, input);
    return sync.step;
}

void stepped_on_foot(game_state& state) {
    net_motion motion = current_motion(state);
    motion.mode = std::abs(state.player.speed) > still_speed ? motion_mode::walk : motion_mode::idle;
    state.motion_sync.stepped = motion;
}

void send_steps(game_state& state) {
    if (!state.motion_sync.unsent.empty() && sends_motion(state) && !holds_motion(state)) {
        push_motion(state, current_motion(state));
    }
}

void reset_motion_sync(game_state& state) {
    const std::uint64_t step = state.motion_sync.step;
    state.motion_sync = motion_sync_state{};
    state.motion_sync.step = step;
}
