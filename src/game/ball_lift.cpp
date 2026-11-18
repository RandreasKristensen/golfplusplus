#include "game/ball_lift.h"

#include "game/address_play.h"
#include "game/course_session.h"
#include "game/interactables.h"
#include "game/mode_dispatch.h"
#include "game/motion_sync.h"
#include "game/my_balls.h"

void begin_ball_hold(game_state& state) {
    const auto near = nearest_interactable(state);
    if (near && near->kind == interactable_kind::cup_ball) {
        send_motion_now(state);
        if (near->index != 0) { pick_up_spare_ball(state, near->index); }
        else { lift_my_ball(state); }
        return;
    }
    state.ball_holding = ball_hold{};
}

bool lift_my_ball(game_state& state) {
    if (state.ball && state.ball->kind != ball_kind::practice) {
        send_motion_now(state);
        return pick_up_cup_ball(state);
    }
    std::optional<glm::vec3> in_play;
    if (state.ball && !state.shot) { in_play = state.ball->position; }
    const std::optional<lift_choice> choice =
        nearest_ball_to_lift(in_play, my_spare_balls(state), on_foot(state), state.player.position, state.tuning);
    if (!choice) { return false; }
    send_motion_now(state);
    return choice->in_play ? pick_up_practice_ball(state) : pick_up_spare_ball(state, choice->spare_id);
}

void update_ball_hold(game_state& state, const game_input& input, const float dt) {
    if (!state.ball_holding) { return; }
    ball_hold& hold = *state.ball_holding;
    if (input.interact_released) {
        const bool lifted = hold.lifted;
        state.ball_holding.reset();
        const std::optional<interactable> near = nearest_interactable(state);
        if (lifted || !near) { return; }
        if (near->kind == interactable_kind::ball) {
            step_up_to_ball(state);
        } else if (near->kind == interactable_kind::spare_ball && play_spare_ball(state, near->index) && !is_online(state)) {
            step_up_to_ball(state);
        }
        return;
    }
    if (!input.interact_held) {
        state.ball_holding.reset();
        return;
    }
    hold.seconds += dt;
    // Each lift is a bend down of its own: holding on lifts the next ball in
    // reach after another hold time, so a cluster is cleared one by one.
    if (hold.seconds >= state.tuning.player.ball_pickup_hold_seconds && lift_my_ball(state)) {
        hold.lifted = true;
        hold.seconds = 0.0f;
    }
}
