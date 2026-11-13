#include "game/online_play.h"
#include "game/social_dap.h"

#include "game/course_session.h"
#include "game/hub_flow.h"
#include "game/hub_session.h"
#include "game/golf_bag.h"
#include "game/address_play.h"
#include "game/motion_sync.h"
#include "game/mode_dispatch.h"
#include "game/remote_players.h"
#include "game/server_errors.h"
#include "game/text_ids.h"

#include <algorithm>
#include <optional>
#include <vector>

#include <glm/geometric.hpp>

namespace {
void snap_to(game_state& state, const net_motion& motion) {
    state.player.position = motion.position;
    state.player.yaw = motion.yaw;
}

// A refused seat change removes its local seat animation.
bool cart_refused(const reducer_failure& refusal) {
    return refusal.reducer == reducer_take_wheel || refusal.reducer == reducer_leave_wheel ||
        refusal.reducer == reducer_board_cart || refusal.reducer == reducer_leave_cart ||
        refusal.error == error_unknown_cart || refusal.error == error_seat_taken || refusal.error == error_not_in_cart;
}

const room_ball* my_server_ball(const game_state& state) {
    const auto ball = state.online.balls.find(state.online.account_id);
    return ball != state.online.balls.end() ? &ball->second : nullptr;
}

// My ball becomes the server's: in play where it lies with its strokes, in its
// cup, or back in my pocket. A hole the server holed my ball on is completed
// here with the server's strokes, as offline.
void adopt_ball_row(game_state& state) {
    const room_ball* server = my_server_ball(state);
    const bool same_hole = server != nullptr && state.ball && server->hole == (state.ball->hole ? static_cast<int>(*state.ball->hole) : -1);
    if (same_hole && server->in_cup && ball_in_play(state)) {
        state.ball->strokes = server->stroke_count;
        if (state.hub) { finish_practice_shot(state); }
        else { complete_current_hole(state); }
        return;
    }
    state.shot.reset();
    state.ball_correction.reset();
    state.flight_path_points.clear();
    state.give_up_prompt.reset();
    state.mode = game_mode::walking;
    state.address = address_state{};
    if (server && server->kind == ball_kind::practice && state.hub) {
        state.ball = my_ball{std::nullopt, server->position, server->stroke_count,
            server->in_cup ? ball_place::in_cup : ball_place::at_rest, ball_kind::practice,
            server->tee_mat >= 0 ? std::optional<std::size_t>(static_cast<std::size_t>(server->tee_mat)) : std::nullopt};
        state.online_hole = online_hole_state{!server->in_cup};
        return;
    }
    if (server == nullptr || server->hole < 0 || static_cast<std::size_t>(server->hole) >= state.holes.size()) {
        state.ball.reset();
        state.online_hole = online_hole_state{};
        return;
    }
    const std::size_t hole = static_cast<std::size_t>(server->hole);
    const ball_place place =
        server->in_cup ? ball_place::in_cup : (server->stroke_count == 0 ? ball_place::teed : ball_place::at_rest);
    if (!same_hole && state.round) {
        state.round->current_hole_index = hole;
    }
    const glm::vec3 position = server->in_cup ? lying_in_cup(state, hole, server->position) : server->position;
    state.ball = my_ball{hole, position, server->stroke_count, place, server->kind};
    state.online_hole = online_hole_state{!server->in_cup};
}

// I go back to where the server last accepted me.
void return_to_server(game_state& state) {
    const room_player* me = my_room_player(state);
    if (me != nullptr && me->motion) {
        snap_to(state, *me->motion);
    }
}

// My bag and the club in my hand as my player_bag row has them.
void adopt_server_bag(game_state& state) {
    const auto row = state.online.bags.find(state.online.account_id);
    if (row == state.online.bags.end()) {
        return;
    }
    state.bag = row->second.place;
    state.picker.reset();
    state.handling.reset();
    for (std::size_t i = 0; i < state.clubs.size(); ++i) {
        if (state.clubs[i].id == row->second.club_in_hand) {
            state.selected_club = i;
        }
    }
    state.server_bag_adopted = true;
}

// The swing over my ball ends where it started: no shot.
bool swing_refused(game_state& state, const reducer_failure& refusal) {
    if (refusal.reducer != reducer_take_shot || state.mode != game_mode::addressing || !state.address.swing_elapsed) {
        return false;
    }
    state.address.swing_elapsed.reset();
    state.address.answer.reset();
    return true;
}

void take_refusals(game_state& state) {
    for (const reducer_failure& refusal : state.online.refusals) {
        if (refusal.reducer == reducer_dap) {
            state.dap_pending.reset();
            state.notice = game_notice{online_error_text_key(refusal.error), 0.0f};
            continue;
        }
        // A ride motion is only for my passenger to watch: a late or lost one
        // is forgotten as the next comes.
        if (refusal.reducer == reducer_update_ride) {
            continue;
        }
        if (swing_refused(state, refusal)) {
            state.notice = game_notice{online_error_text_key(refusal.error), 0.0f};
            continue;
        }
        if (refusal.reducer == reducer_place_bag || refusal.reducer == reducer_take_club) {
            adopt_server_bag(state);
        }
        state.motion_sync.last_refusal = refusal.reducer + ":" + refusal.error;
        if (cart_refused(refusal)) {
            std::optional<cart_step> from;
            if (state.ride && state.ride->step && state.ride->step->getting_in) {
                from = *state.ride->step;
            }
            adopt_server_cart(state);
            leave_cart_at_once(state);
            if (refusal.reducer == reducer_take_wheel && from) {
                state.player.position = from->from;
                state.player.yaw = from->from_yaw;
            }
        }
        if (refusal.reducer == reducer_update_motion) {
            refuse_motion(state);
        }
        if (refusal.reducer == reducer_right_cart) {
            adopt_server_cart(state);
        }
        const bool ball_refused = refusal.reducer == reducer_take_shot || refusal.reducer == reducer_tee_up ||
            refusal.reducer == reducer_place_practice_ball || refusal.reducer == reducer_pick_up_ball || refusal.reducer == reducer_give_up_hole ||
            refusal.reducer == "start_booked_round";
        if (ball_refused) {
            state.hub_flow.ball_request.reset();
            adopt_ball_row(state);
        }
        if (refusal.reducer == "start_booked_round" || refusal.reducer == reducer_tee_up || refusal.reducer == reducer_claim_collectible ||
            (refusal.reducer == reducer_update_motion && !driving_cart(state))) {
            return_to_server(state);
        }
        state.notice = game_notice{online_error_text_key(refusal.error), 0.0f};
    }
    state.online.refusals.clear();
}

// My shot as the server played it: where it rests wins.
void reconcile_my_shot(game_state& state, const room_shot& shot) {
    if (!ball_in_play(state) || shot.hole != (state.ball->hole ? static_cast<int>(*state.ball->hole) : -1)) {
        return;
    }
    if (shot.stroke != state.ball->strokes) {
        return;  // an older shot, already corrected
    }
    const float off_by = state.tuning.net.shot_correction_distance;
    if (state.shot) {
        const glm::vec3 offset = shot.rest - state.shot->result.rest_position;
        if (glm::length(offset) > off_by) {
            state.shot->correction = offset;
        }
    } else if (glm::length(shot.rest - state.ball->position) > off_by) {
        state.ball_correction = ball_blend{state.ball->position, shot.rest, 0.0f};
    }
}

// The answer to the swing I am making: its next stroke on my ball's hole.
bool answers_my_swing(const game_state& state, const room_shot& shot) {
    return state.mode == game_mode::addressing && state.address.swing_elapsed && !state.address.answer &&
        ball_in_play(state) && shot.hole == (state.ball->hole ? static_cast<int>(*state.ball->hole) : -1) && shot.stroke == state.ball->strokes + 1;
}

// My shots answer my swing, or are reconciled; others' shots and item uses
// play (remote_players.h). Scores come from member rows, never these events.
void take_room_events(game_state& state) {
    for (const room_shot& shot : state.online.shots) {
        if (shot.account_id == state.online.account_id) {
            if (answers_my_swing(state, shot)) {
                state.address.answer = shot;
            } else {
                reconcile_my_shot(state, shot);
            }
        } else {
            start_remote_shot(state, shot);
        }
    }
    state.online.shots.clear();
    for (const room_item_use& use : state.online.item_uses) {
        start_remote_item_use(state, use);
    }
    state.online.item_uses.clear();
}

void update_ball_correction(game_state& state, const float dt) {
    if (!state.ball_correction) {
        return;
    }
    ball_blend& blend = *state.ball_correction;
    blend.elapsed += dt;
    const float seconds = state.tuning.net.shot_correction_seconds;
    const float t = seconds > 0.0f ? std::min(1.0f, blend.elapsed / seconds) : 1.0f;
    if (state.ball) {
        state.ball->position = blend.from + (blend.to - blend.from) * t;
    }
    if (t >= 1.0f) {
        state.ball_correction.reset();
    }
}

// The server has my ball on its hole, then no longer in play there: holed
// (score it, as offline) or given up (no score). Not while my swing is under
// way: a holed answer comes with my ball row already in the cup, and the putt
// plays out first.
void follow_server_hole(game_state& state) {
    if (!ball_in_play(state) || state.address.swing_elapsed || shot_playing(state) || state.ball_correction) {
        return;
    }
    const room_ball* server = my_server_ball(state);
    if (server != nullptr && server->hole == (state.ball->hole ? static_cast<int>(*state.ball->hole) : -1) && !server->in_cup) {
        state.online_hole.entered = true;
        return;
    }
    if (state.online_hole.entered) {
        adopt_ball_row(state);
    }
}

// Each green's flag as the room's rows have it.
void follow_server_flags(game_state& state) {
    std::fill(state.flags_in.begin(), state.flags_in.end(), true);
    for (const int out : state.online.flags_out) {
        if (out >= 0 && static_cast<std::size_t>(out) < state.flags_in.size()) {
            state.flags_in[static_cast<std::size_t>(out)] = false;
        }
    }
}

bool preparation_ready(const game_state& state) {
    if (!state.hub_flow.ball_request || !state.online.room_ready || state.online.ball_updates <= state.hub_flow.ball_updates) { return false; }
    const auto ball = my_server_ball(state);
    const auto me = my_room_player(state);
    if (*state.hub_flow.ball_request == net_command_type::pick_up_ball) { return ball == nullptr; }
    if (ball && *state.hub_flow.ball_request == net_command_type::place_practice_ball) {
        return ball->kind == ball_kind::practice && ball->tee_mat < 0 && ball->stroke_count == 0 && !ball->in_cup;
    }
    if (!ball || !me || !me->motion) { return false; }
    if (ball->kind == ball_kind::practice && state.hub && ball->tee_mat >= 0 &&
        static_cast<std::size_t>(ball->tee_mat) < state.hub->tee_mats.size()) {
        const auto& mat = state.hub->tee_mats[static_cast<std::size_t>(ball->tee_mat)];
        return glm::distance(me->motion->position, tee_stance_position(state.area, ball->position, mat.aim,
            state.tuning.player.ball_stand_off_distance)) <= state.tuning.net.shot_correction_distance;
    }
    if (ball->hole < 0 || static_cast<std::size_t>(ball->hole) >= state.area.cups.size()) { return false; }
    if (*state.hub_flow.ball_request == net_command_type::start_booked_round && (!me->round_started || ball->kind != ball_kind::scored)) { return false; }
    const auto stance = tee_stance_position(state.area, ball->position, state.area.cups[static_cast<std::size_t>(ball->hole)].centre,
        state.tuning.player.ball_stand_off_distance);
    return glm::distance(me->motion->position, stance) <= state.tuning.net.shot_correction_distance;
}

void age_notice(game_state& state, const float dt) {
    if (!state.notice) {
        return;
    }
    state.notice->age += dt;
    if (state.notice->age >= state.tuning.net.notice_seconds) {
        state.notice.reset();
    }
}
}

void adopt_online_arrival(game_state& state) {
    clear_online_presentation(state);
    adopt_ball_row(state);
    return_to_server(state);
    adopt_server_bag(state);
    if (const auto me = my_room_player(state); me && state.round) {
        for (std::size_t i = 0; i < me->round_strokes.size() && i < state.round->strokes.size(); ++i) {
            state.round->strokes[i] = me->round_strokes[i] > 0 ? std::optional<int>(me->round_strokes[i]) : std::nullopt;
        }
    }
}

void update_online_play(game_state& state, const float dt) {
    age_notice(state, dt);
    if (!is_online(state)) {
        return;
    }
    reconcile_dap(state);
    take_refusals(state);
    if (state.hub_flow.ball_request == net_command_type::start_booked_round && reservation_expired(state)) {
        // Expiry can win after preparation committed but before its rows arrive.
        state.hub_flow.ball_request.reset();
        adopt_ball_row(state);
        return_to_server(state);
        reset_motion_sync(state);
        state.notice = game_notice{text_online_error_booking_expired, 0};
    }
    if (preparation_ready(state)) {
        const bool practice_action = state.hub_flow.ball_request == net_command_type::place_practice_ball ||
            state.hub_flow.ball_request == net_command_type::pick_up_ball;
        state.hub_flow.ball_request.reset();
        adopt_ball_row(state);
        if (!practice_action) { return_to_server(state); reset_motion_sync(state); }
        // The warmup station supplies its putter in the same server transaction.
        if (course_warmup(state)) { select_putter(state); }
        if (!practice_action && ball_in_play(state)) { step_up_to_ball(state); }
    }
    if (state.hub_flow.ball_request) { return; }
    if (!state.server_bag_adopted) {
        adopt_server_bag(state);
    }
    sync_golfer(state);
    // Restore an abandoned round on reconnect, without ending a live shot's
    // playback early when the server has already scored its result.
    if (state.online.room_ready && !state.ball) {
        if (const auto me = my_room_player(state); me && state.round && me->round_strokes.size() == state.round->strokes.size()) {
            for (std::size_t i = 0; i < me->round_strokes.size(); ++i) {
                state.round->strokes[i] = me->round_strokes[i] > 0 ? std::optional<int>(me->round_strokes[i]) : std::nullopt;
            }
        }
    }
    if (const auto server = my_server_ball(state); state.ball && server && server->kind != state.ball->kind &&
        !state.hub_flow.ball_request) {
        adopt_ball_row(state); return_to_server(state);
    }
    follow_server_flags(state);
    follow_server_carts(state);
    reconcile_motion(state, dt);
    take_room_events(state);
    update_ball_correction(state, dt);
    follow_server_hole(state);
}

void watch_room(game_state& state, const float dt) {
    age_notice(state, dt);
    if (!is_online(state)) {
        return;
    }
    reconcile_dap(state);
    take_refusals(state);  // a refused move puts me back
    follow_server_carts(state);
    reconcile_motion(state, dt);
    take_room_events(state);
}
