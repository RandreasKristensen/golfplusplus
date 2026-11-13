#include "game/game_state.h"
#include "game/address_play.h"
#include "game/course_session.h"
#include "game/golf_bag.h"
#include "game/hub_ball.h"
#include "game/hub_flow.h"
#include "game/hub_session.h"
#include "game/interactables.h"
#include "game/mode_dispatch.h"
#include "game/motion_sync.h"
#include "game/pacing.h"
#include "game/rangefinder.h"
#include "game/green_read.h"
#include "game/social_dap.h"
#include "game/walking.h"
#include "physics/vector_math.h"
#include <algorithm>

namespace {
// F with what is in reach (game/interactables.h). Teeing up also takes up the
// ball teed there, like any press beside the ball; with my ball in play on
// another hole the tee asks first (give_up_prompt). Online the server checks
// reach from where it last had me: it hears where I stand first.
void interact(game_state& state) {
    const std::optional<interactable> near = nearest_interactable(state);
    if (!near) {
        return;
    }
    switch (near->kind) {
    case interactable_kind::dap:
        request_dap(state, near->index); return;
    case interactable_kind::tee_book:
        state.hub_flow.screen = hub_screen::book; return;
    case interactable_kind::bus:
        state.hub_flow.screen = hub_screen::bus; return;
    case interactable_kind::booked_start:
        request_ball_preparation(state, net_command_type::start_booked_round); return;
    case interactable_kind::warmup:
        request_ball_preparation(state, net_command_type::tee_up, near->index, true); return;
    case interactable_kind::fallen_bag:
        take_bag_on_back(state);
        return;
    case interactable_kind::tipped_cart:
        start_righting(state, near->tipped);
        return;
    case interactable_kind::practice_ball:
        pick_up_practice_ball(state); return;
    case interactable_kind::ball:
        step_up_to_ball(state);
        return;
    case interactable_kind::bag:
        open_club_picker(state);
        return;
    case interactable_kind::cup_ball:
        send_motion_now(state);
        pick_up_cup_ball(state);
        return;
    case interactable_kind::collectible:
        send_motion_now(state);
        record_collectible_claim(state, state.world.collectibles[near->index]);
        return;
    case interactable_kind::tee: {
        if (state.hub) {
            const bool again = state.ball && state.ball->tee_mat == near->index;
            const address_state last = state.address;
            send_motion_now(state);
            if (tee_up_hub(state, near->index)) {
                step_up_to_ball(state);
                if (again) { set_up_as(state, last); }
            }
        } else if (ball_in_play(state)) {
            state.give_up_prompt = near->index;
        } else {
            send_motion_now(state);
            if (tee_up(state, near->index, false)) { step_up_to_ball(state); }
        }
        return;
    }
    }
}

// The give-up question at a tee: F gives my hole up and tees up there. Esc,
// or walking out of the tee's reach, keeps playing it. True when F answered.
bool answer_give_up_prompt(game_state& state, const game_input& input) {
    if (!state.give_up_prompt) {
        return false;
    }
    const std::size_t tee = *state.give_up_prompt;
    const std::vector<interactable> near = interactables_in_reach(state);
    const bool in_reach = std::any_of(near.begin(), near.end(), [tee](const interactable& thing) {
        return thing.kind == interactable_kind::tee && thing.index == tee;
    });
    if (!in_reach || !ball_in_play(state) || !on_foot(state)) {
        state.give_up_prompt.reset();
        return false;
    }
    if (!input.interact) {
        return false;
    }
    state.give_up_prompt.reset();
    send_motion_now(state);
    if (tee_up(state, tee, true)) {
        step_up_to_ball(state);
    }
    return true;
}

// The arrow keys and the mouse: on foot they turn me, in a cart my head,
// slower through the rangefinder's zoom. With the club picker open the
// arrows pick a club instead.
void look_around(game_state& state, const game_input& input, const float dt) {
    const player_tuning& tuning = state.tuning.player;
    const float slow = 1.0f / rangefinder_magnification(state);
    const float arrows = state.picker ? 0.0f : 1.0f;
    const float turn = tuning.turn_rate * turn_direction(input) * arrows * slow;
    const float look_yaw = input.look_yaw * slow;
    if (state.ride) {
        state.look.head_yaw = wrap_angle(state.look.head_yaw + turn * dt + look_yaw);
    } else {
        state.player.turn_rate = turn;
        state.player.yaw = wrap_angle(state.player.yaw + turn * dt + look_yaw);
    }
    const float pitch =
        state.look.pitch + (tuning.look_pitch_rate * look_pitch_direction(input) * arrows * dt + input.look_pitch) * slow;
    state.look.pitch = std::clamp(pitch, -tuning.max_look_pitch, tuning.max_look_pitch);
}

}

void update_walking(game_state& state, const game_input& input, const float dt, frame_profile* profile) {
    look_around(state, input, dt);
    if (dap_holds_movement(state)) { stop_walking(state); return; }
    if (state.ride) {
        stop_walking(state);
        update_cart(state, input, dt, profile);
        return;
    }

    const bool picking = update_club_picker(state, input);
    if (picking && state.picker) {
        stop_walking(state);
        return;
    }
    if (!picking && !state.handling && input.swing && state.ball && state.ball->kind == ball_kind::practice &&
        state.ball->place != ball_place::in_cup && !state.shot &&
        practice_pickup_in_reach(state.ball->kind, true, on_foot(state), state.player.position, state.ball->position, state.tuning)) {
        step_up_to_ball(state);
        return;
    }
    const glm::vec3 before = state.player.position;
    // Reading the green keeps its slow steps, without a sprint or hop.
    game_input moving = input;
    if (crouch_held(state, input)) {
        moving.run_held = false;
        moving.jump = false;
    }
    update_pacing(state, moving);
    walk_steps(state, moving, dt, profile);
    walk_into_seat(state, moving, dt);
    if (!picking && !state.handling && input.move_bag) {
        move_bag(state);
    }
    if (input.toggle_flag) {
        toggle_flag_in_reach(state);
    }

    const float meters = horizontal_distance(state.player.position, before) * state.tuning.scale.meters_per_world_unit;
    record_movement(state, state.rewards.walking, state.walk_meters_pending, meters);

    if (!picking && !state.handling && !answer_give_up_prompt(state, input) && input.interact) {
        interact(state);
    }
    // Taking up the ball or a seat puts me back on my feet.
    if (!state.player.vertical.grounded && (state.mode != game_mode::walking || state.ride)) {
        state.player.vertical = vertical_body{};
        if (!state.ride) {
            state.player.position.y = terrain_height(state.area, state.player.position, profile);
        }
    }
}

