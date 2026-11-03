#include "game/motion_sync.h"

#include "game/body_contact.h"
#include "game/game_state.h"
#include "game/mode_dispatch.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <vector>

#include <glm/geometric.hpp>

namespace {
const room_player* self(const game_state& state) {
    const auto me = state.online.players.find(state.online.account_id);
    return me == state.online.players.end() ? nullptr : &me->second;
}

bool differs(const cart_pose& a, const cart_pose& b, const net_tuning& tuning) {
    return glm::length(a.position - b.position) > tuning.cart_reconcile_distance ||
        std::abs(wrap_angle(a.yaw - b.yaw)) > tuning.cart_reconcile_yaw;
}

void report(game_state& state, const char* thing, const glm::vec3& predicted, const glm::vec3& authoritative) {
    motion_sync_state& sync = state.motion_sync;
    ++sync.breaches;
    sync.reports.push_back(sync_breach{thing, predicted, authoritative, sync.acknowledged, sync.last_refusal});
}

void seat_driver(game_state& state) {
    state.player.position = seat_position(state.cart.pose, cart_seat::driver, state.tuning.cart);
    state.player.yaw = state.cart.pose.yaw;
    state.player.speed = my_cart_speed(state);
    state.player.turn_rate = state.cart.motion.yaw_rate * state.tuning.cart.time_scale;
}

void discard_pending(game_state& state) {
    state.motion_sync.history.clear();
    state.motion_sync.cart_steps.clear();
    state.motion_sync.unsent.clear();
    state.motion_sync.stepped.reset();
    state.motion_sync.last_sent.reset();
    state.motion_sync.queued.reset();
    auto& commands = state.net_commands;
    commands.erase(std::remove_if(commands.begin(), commands.end(), [](const net_command& command) {
        return command.type == net_command_type::motion || command.type == net_command_type::ride_motion;
    }), commands.end());
}

void replay_cart(game_state& state, const predicted_motion& prediction, const room_cart& row) {
    // Keep every part of the fixed-step state, including lateral drift and
    // vertical velocity; only the server's pose and condition replace it.
    const float unstepped = state.cart.unstepped_seconds;
    const float wetness = state.weather.wetness;
    state.cart = prediction.cart;
    const float turn = wrap_angle(row.yaw - state.cart.pose.yaw);
    const glm::vec2 velocity = state.cart.motion.velocity;
    state.cart.motion.velocity = glm::vec2(velocity.x * std::cos(turn) + velocity.y * std::sin(turn),
                                           velocity.y * std::cos(turn) - velocity.x * std::sin(turn));
    state.cart.pose = cart_pose{row.position, row.yaw};
    state.cart.motion.yaw = row.yaw;
    state.cart.motion.condition = row.condition;
    state.cart.vertical.y = row.position.y;
    const std::vector<solid> others = cart_obstacles(state);
    // Replay uses the live cart step; effects and controls are those of each original step.
    const std::size_t sounds = state.audio_events.size();
    for (const predicted_cart_step& step : state.motion_sync.cart_steps) {
        if (step.number > prediction.step) {
            state.weather.wetness = step.wetness;
            state.cart.steer = step.controls.steer;
            step_predicted_cart(state, step.controls, step.spec, step.grip, others, nullptr);
        }
    }
    state.audio_events.resize(sounds);
    state.weather.wetness = wetness;
    state.cart.unstepped_seconds = unstepped;
    if (driving_cart(state)) {
        seat_driver(state);
    }
}
}

void motion_sent(game_state& state, const net_command& command, const double id) {
    motion_sync_state& sync = state.motion_sync;
    net_motion motion = command.motion;
    motion.client_time = id;
    sync.last_sent = motion;
    sync.queued.reset();
    sync.since_sent = 0.0f;
    if (motion.mode != motion_mode::cart || driving_cart(state)) {
        sync.history.push_back(predicted_motion{motion, sync.clock, command.last_step, command.cart_prediction.value_or(state.cart),
                                                command.placed});
    }
}

void refuse_motion(game_state& state) {
    const bool driving = driving_cart(state);
    adopt_server_cart(state);
    if (driving_cart(state)) {
        seat_driver(state);
    } else if (const room_player* me = self(state); me && me->motion) {
        state.player.position = me->motion->position;
        state.player.yaw = me->motion->yaw;
    }
    if (driving && state.cart.motion.condition != cart_condition::upright) {
        leave_cart_at_once(state);
    }
    discard_pending(state);
}

void reconcile_motion(game_state& state, const float dt) {
    motion_sync_state& sync = state.motion_sync;
    sync.clock += dt;
    if (!driving_cart(state)) {
        sync.cart_steps.clear();
        sync.history.erase(std::remove_if(sync.history.begin(), sync.history.end(), [](const predicted_motion& pending) {
            return pending.motion.mode == motion_mode::cart;
        }), sync.history.end());
    }
    const auto mine = state.online.carts.find(state.online.account_id);
    if (mine != state.online.carts.end()) {
        const room_cart& row = mine->second;
        const cart_pose authoritative{row.position, row.yaw};
        const bool changed = !sync.observed_cart || row.motion_id != sync.observed_cart->motion_id ||
            differs(authoritative, cart_pose{sync.observed_cart->position, sync.observed_cart->yaw}, state.tuning.net) ||
            row.condition != sync.observed_cart->condition || row.driver != sync.observed_cart->driver;
        if (driving_cart(state) && changed) {
            const auto found = std::find_if(sync.history.begin(), sync.history.end(), [&row](const predicted_motion& p) {
                return p.motion.client_time == row.motion_id && p.motion.mode == motion_mode::cart;
            });
            if (found != sync.history.end()) {
                const cart_pose& predicted = found->cart.pose;
                if (differs(predicted, authoritative, state.tuning.net) || row.condition != state.cart.motion.condition) {
                    replay_cart(state, *found, row);
                }
                while (!sync.cart_steps.empty() && sync.cart_steps.front().number <= found->step) {
                    sync.cart_steps.pop_front();
                }
                // The server's steps tipped or sank it under me.
                if (row.condition != cart_condition::upright && driving_cart(state)) {
                    leave_cart_at_once(state);
                }
            } else if (sync.observed_cart) {
                // A tow or administrative correction is authoritative even without a new motion id.
                adopt_server_cart(state);
                if (driving_cart(state)) {
                    seat_driver(state);
                }
                discard_pending(state);
            }
        }
        sync.cart_acknowledged = row.motion_id;
        sync.observed_cart = row;
        if (!driving_cart(state)) {
            const auto shown = shown_cart(state, 0);
            if (shown && differs(*shown, authoritative, state.tuning.net)) {
                report(state, "cart", shown->position, row.position);
                adopt_server_cart(state);
            }
        }
    }
    const room_player* me = self(state);
    if (me && me->motion && me->motion->client_time > sync.acknowledged) {
        const net_motion& row = *me->motion;
        const auto found = std::find_if(sync.history.begin(), sync.history.end(), [&row](const predicted_motion& p) {
            return p.motion.client_time == row.client_time;
        });
        if (found != sync.history.end() && on_foot(state) && row.mode == found->motion.mode &&
            (row.mode == motion_mode::walk || row.mode == motion_mode::idle)) {
            const glm::vec3 correction = row.position - found->motion.position;
            const float yaw = wrap_angle(row.yaw - found->motion.yaw);
            // Where I was put since, which the row may show already: still
            // unnoticed by a step when it is my last motion.
            glm::vec3 placed = found->placed;
            if (std::next(found) == sync.history.end() && sync.stepped) {
                placed += state.player.position - sync.stepped->position;
            }
            const float tolerance = state.tuning.net.cart_reconcile_distance;
            const bool put_there = placed != glm::vec3(0.0f) &&
                glm::length(row.position - (found->motion.position + placed)) <= tolerance;
            if (!put_there && (glm::length(correction) > tolerance || std::abs(yaw) > state.tuning.net.cart_reconcile_yaw)) {
                // Keep the movement after this id, replayed from the corrected position.
                const glm::vec3 travel = state.player.position - found->motion.position;
                state.player.position = row.position + travel;
                keep_walker_clear(state, row.position);
                state.player.yaw = wrap_angle(state.player.yaw + yaw);
                for (predicted_motion& pending : sync.history) {
                    if (pending.motion.client_time > row.client_time) {
                        pending.motion.position += correction;
                        pending.motion.yaw = wrap_angle(pending.motion.yaw + yaw);
                    }
                }
                if (sync.stepped) {
                    sync.stepped->position = state.player.position;
                }
                sync.unsent_start += correction;
            }
        }
        sync.acknowledged = row.client_time;
    }
    // A cart and its avatar are acknowledged independently, even if their rows arrive in separate frames.
    while (!sync.history.empty()) {
        const predicted_motion& first = sync.history.front();
        if (first.motion.client_time > sync.acknowledged ||
            (first.motion.mode == motion_mode::cart && first.motion.client_time > sync.cart_acknowledged)) {
            break;
        }
        sync.history.pop_front();
    }
    if (!sync.history.empty() && sync.clock - sync.history.front().sent_at > state.tuning.net.max_unacked_seconds) {
        const glm::vec3 authoritative = me && me->motion ? me->motion->position : state.player.position;
        report(state, driving_cart(state) ? "cart_timeout" : "avatar_timeout", state.player.position, authoritative);
        refuse_motion(state);
    }
    const bool waiting = state.ride && !state.ride->confirmed;
    sync.seat_wait = waiting ? sync.seat_wait + dt : 0.0f;
    if (waiting && sync.seat_wait > state.tuning.net.max_unacked_seconds) {
        report(state, "seat_timeout", state.player.position, me && me->motion ? me->motion->position : state.player.position);
        adopt_server_cart(state);
        leave_cart_at_once(state);
        refuse_motion(state);
        sync.seat_wait = 0.0f;
    }
}
