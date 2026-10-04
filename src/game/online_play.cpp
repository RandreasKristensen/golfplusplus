#include "game/online_play.h"

#include "game/course_session.h"
#include "game/mode_dispatch.h"
#include "game/remote_players.h"
#include "game/text_ids.h"

#include <algorithm>
#include <vector>

#include <glm/geometric.hpp>

namespace {
void snap_to(game_state& state, const net_motion& motion) {
    state.player.position = motion.position;
    state.player.yaw = motion.yaw;
    state.cart.yaw = motion.yaw;
}

// Puts this client where the server has the player: their zone (leaving a
// hole the other has left, on either side) and last accepted position. A
// hole the server holed my ball on is completed, as offline.
void return_to_server(game_state& state) {
    const room_player* me = my_room_player(state);
    if (me == nullptr || !me->motion) {
        return;
    }
    if (me->zone != local_zone(state)) {
        if (me->zone != hub_zone) {
            record_hole_given_up(state);
        }
        if (state.hole && state.online_hole.holed && me->zone == hub_zone) {
            complete_current_hole(state);
            return;
        }
        if (state.hole) {
            abandon_hole(state, me->motion->position);
        }
    }
    snap_to(state, *me->motion);
}

// A refused shot or retee: the ball goes back to the server's, with its
// strokes; without one, the player goes back to the server too.
void restore_ball(game_state& state) {
    const auto ball = state.online.balls.find(state.online.account_id);
    if (ball == state.online.balls.end() || !state.hole || ball->second.zone != static_cast<int>(state.hole->index)) {
        return_to_server(state);
        return;
    }
    state.shot.reset();
    state.ball_correction.reset();
    state.flight_path_points.clear();
    state.mode = game_mode::walking;
    state.ball.position = ball->second.position;
    state.stroke_count = ball->second.stroke_count;
}

void take_refusals(game_state& state) {
    for (const reducer_failure& refusal : state.online.refusals) {
        if (refusal.reducer == "take_shot" || refusal.reducer == "retee") {
            restore_ball(state);
        } else if (refusal.reducer == "enter_hole" || refusal.reducer == "claim_collectible" ||
                   (refusal.reducer == "update_motion" && refusal.error == "wrong_zone")) {
            return_to_server(state);
        } else if (refusal.reducer == "update_motion") {
            // Too fast: back to the last accepted position. Only when the
            // zones agree; a zone change on its way is not undone here.
            const room_player* me = my_room_player(state);
            if (me != nullptr && me->motion && me->zone == local_zone(state)) {
                snap_to(state, *me->motion);
            }
        }
        state.notice = game_notice{online_error_text_key(refusal.error), 0.0f};
    }
    state.online.refusals.clear();
}

// My shot as the server played it: where it rests wins.
void reconcile_my_shot(game_state& state, const room_shot& shot) {
    if (!state.hole || shot.zone != static_cast<int>(state.hole->index)) {
        return;
    }
    state.online_hole.holed = state.online_hole.holed || shot.holed;
    if (shot.stroke != state.stroke_count) {
        return;  // an older shot, already corrected
    }
    const float off_by = state.tuning.net.shot_correction_distance;
    if (state.shot) {
        const glm::vec3 offset = shot.rest - state.shot->result.rest_position;
        if (glm::length(offset) > off_by) {
            state.shot->correction = offset;
        }
    } else if (glm::length(shot.rest - state.ball.position) > off_by) {
        state.ball_correction = ball_blend{state.ball.position, shot.rest, 0.0f};
    }
}

void take_shots(game_state& state) {
    for (const room_shot& shot : state.online.shots) {
        if (shot.account_id == state.online.account_id) {
            reconcile_my_shot(state, shot);
        } else {
            start_remote_shot(state, shot);
        }
    }
    state.online.shots.clear();
}

void update_ball_correction(game_state& state, const float dt) {
    if (!state.ball_correction) {
        return;
    }
    ball_blend& blend = *state.ball_correction;
    blend.elapsed += dt;
    const float seconds = state.tuning.net.shot_correction_seconds;
    const float t = seconds > 0.0f ? std::min(1.0f, blend.elapsed / seconds) : 1.0f;
    state.ball.position = blend.from + (blend.to - blend.from) * t;
    if (t >= 1.0f) {
        state.ball_correction.reset();
    }
}

// The server has me on the hole, then back in the hub: holed there (score
// it, as offline) or given up (no score).
void follow_server_hole(game_state& state) {
    const room_player* me = my_room_player(state);
    if (!state.hole || me == nullptr) {
        return;
    }
    if (me->zone == static_cast<int>(state.hole->index)) {
        state.online_hole.entered = true;
        return;
    }
    if (state.online_hole.entered && me->zone == hub_zone && !shot_playing(state) && !state.ball_correction) {
        return_to_server(state);
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
    take_shots(state);
    update_ball_correction(state, dt);
    follow_server_hole(state);
}
