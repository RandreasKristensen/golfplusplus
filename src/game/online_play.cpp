#include "game/online_play.h"

#include "game/course_session.h"
#include "game/group_round.h"
#include "game/mode_dispatch.h"
#include "game/remote_players.h"
#include "game/server_errors.h"
#include "game/text_ids.h"

#include <algorithm>
#include <vector>

#include <glm/geometric.hpp>

namespace {
void snap_to(game_state& state, const net_motion& motion) {
    state.player.position = motion.position;
    state.player.yaw = motion.yaw;
    if (driving_cart(state)) {
        state.cart.pose = cart_around_seat(motion.position, motion.yaw, cart_seat::driver, state.tuning.cart);
    }
}

// A refused seat, or a cart move the server would not take: on foot again.
bool cart_refused(const reducer_failure& refusal) {
    return refusal.reducer == reducer_board_cart || refusal.reducer == reducer_leave_cart ||
        refusal.error == error_unknown_cart || refusal.error == error_seat_taken || refusal.error == error_not_in_cart ||
        (refusal.reducer == reducer_update_motion && refusal.error == error_too_far);
}

const room_ball* my_server_ball(const game_state& state) {
    const auto ball = state.online.balls.find(state.online.account_id);
    return ball != state.online.balls.end() ? &ball->second : nullptr;
}

// My ball becomes the server's: in play where it lies with its strokes, in its
// cup, or back in my pocket. A hole the server holed my ball on is completed
// here with the server's strokes, as offline.
void adopt_server_ball(game_state& state) {
    const room_ball* server = my_server_ball(state);
    const bool same_hole = server != nullptr && state.ball && server->hole == static_cast<int>(state.ball->hole);
    if (same_hole && server->in_cup && ball_in_play(state)) {
        state.ball->strokes = server->stroke_count;
        complete_current_hole(state);
        return;
    }
    state.shot.reset();
    state.ball_correction.reset();
    state.flight_path_points.clear();
    state.give_up_prompt.reset();
    state.mode = game_mode::walking;
    state.address = address_state{};
    if (server == nullptr || server->hole < 0 || static_cast<std::size_t>(server->hole) >= state.holes.size()) {
        state.ball.reset();
        state.online_hole = online_hole_state{};
        return;
    }
    const std::size_t hole = static_cast<std::size_t>(server->hole);
    const ball_place place =
        server->in_cup ? ball_place::in_cup : (server->stroke_count == 0 ? ball_place::teed : ball_place::at_rest);
    if (!same_hole) {
        state.round.current_hole_index = hole;
    }
    state.ball = my_ball{hole, server->position, server->stroke_count, place};
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
        if (swing_refused(state, refusal)) {
            state.notice = game_notice{online_error_text_key(refusal.error), 0.0f};
            continue;
        }
        if (refusal.reducer == reducer_place_bag || refusal.reducer == reducer_take_club) {
            adopt_server_bag(state);
        }
        if (cart_refused(refusal)) {
            leave_cart_at_once(state);
        }
        if (refusal.reducer == reducer_overturn_cart || refusal.reducer == reducer_right_cart) {
            adopt_server_cart(state);
        }
        const bool ball_refused = refusal.reducer == reducer_take_shot || refusal.reducer == reducer_tee_up ||
            refusal.reducer == reducer_pick_up_ball || refusal.reducer == reducer_give_up_hole;
        if (ball_refused) {
            adopt_server_ball(state);
        }
        if (refusal.reducer == reducer_tee_up || refusal.reducer == reducer_claim_collectible ||
            refusal.reducer == reducer_update_motion) {
            return_to_server(state);
        }
        state.notice = game_notice{online_error_text_key(refusal.error), 0.0f};
    }
    state.online.refusals.clear();
}

// My shot as the server played it: where it rests wins.
void reconcile_my_shot(game_state& state, const room_shot& shot) {
    if (!ball_in_play(state) || shot.hole != static_cast<int>(state.ball->hole)) {
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
        ball_in_play(state) && shot.hole == static_cast<int>(state.ball->hole) && shot.stroke == state.ball->strokes + 1;
}

// My shots answer my swing, or are reconciled; others' shots and item uses
// play (remote_players.h). Every member's strokes are kept for the group's
// scorecard.
void take_room_events(game_state& state) {
    note_round_strokes(state);
    for (const room_shot& shot : state.online.shots) {
        note_holed_shot(state, shot);
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
// (score it, as offline) or given up (no score).
void follow_server_hole(game_state& state) {
    if (!ball_in_play(state) || shot_playing(state) || state.ball_correction) {
        return;
    }
    const room_ball* server = my_server_ball(state);
    if (server != nullptr && server->hole == static_cast<int>(state.ball->hole) && !server->in_cup) {
        state.online_hole.entered = true;
        return;
    }
    if (state.online_hole.entered) {
        adopt_server_ball(state);
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

void update_online_play(game_state& state, const float dt) {
    age_notice(state, dt);
    if (!is_online(state)) {
        return;
    }
    take_refusals(state);
    if (!state.server_bag_adopted) {
        adopt_server_bag(state);
    }
    sync_golfer(state);
    follow_server_flags(state);
    follow_server_carts(state);
    take_room_events(state);
    update_ball_correction(state, dt);
    follow_server_hole(state);
}

void watch_room(game_state& state, const float dt) {
    age_notice(state, dt);
    if (!is_online(state)) {
        return;
    }
    take_refusals(state);  // a refused move puts me back
    follow_server_carts(state);
    take_room_events(state);
}
