#include "game/contact_play.h"

#include "game/game_state.h"
#include "game/golf_cart.h"
#include "game/mode_dispatch.h"
#include "game/remote_players.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr std::int64_t micros_per_milli = 1000;

std::int64_t hold_ms(const net_tuning& tuning) {
    return static_cast<std::int64_t>(std::llround(tuning.contact_prediction_hold_seconds * 1000.0f));
}

// A shot playing here, as its replay with `predicted` after its server
// contacts, drawn from `drawn` and blending to the new path. The sound of the
// predicted contact is left to be heard, as it comes.
template <typename Playing>
void replay_with(const game_state& state, Playing& playing, const glm::vec3& drawn, const std::optional<flight_contact>& predicted,
                 const glm::vec3& correction) {
    std::vector<flight_contact> contacts = playing.contacts;
    if (predicted) {
        contacts.push_back(*predicted);
    }
    playing.result = room_shot_result(state, *playing.source, contacts);
    playing.correction = correction;
    const float duration = playing.result.duration;
    const float at = std::clamp(playing.elapsed, 0.0f, duration);
    const float progress = duration > 0.0f ? at / duration : 1.0f;
    playing.blend = contact_blend{drawn - (shot_position_at(playing.result, at) + correction * progress), 0.0f,
                                  state.tuning.net.shot_correction_seconds};
    playing.next_event = static_cast<std::size_t>(std::count_if(
        playing.result.events.begin(), playing.result.events.end(), [&](const shot_event& event) {
            const bool the_hit = predicted && event.kind == shot_event_kind::person_hit &&
                std::abs(event.time - predicted->time) <= shot_step_seconds;
            return event.time <= playing.elapsed && !the_hit;
        }));
}

// The flight as the rows have it, built when first needed and again when a contact row joined it.
template <typename Playing>
const ball_flight& flight_of(const game_state& state, Playing& playing) {
    contact_watch& watch = playing.watch;
    if (!watch.flight || watch.flight_contacts != playing.contacts.size()) {
        const room_shot& source = *playing.source;
        watch.flight = replay_contacts(source.record, source.golfer, room_shot_course(state, source), state.tuning, state.clubs,
                                       playing.contacts);
        watch.flight_contacts = playing.contacts.size();
    }
    return *watch.flight;
}

struct me_now {
    snapshot_entry entry;
    std::int64_t now_ms = 0;
    float reach = 0.0f;  // net.contact_send_meters in world units
};

// One flight playing here: whether it passes near me, and my path against it.
template <typename Playing>
void follow_flight(game_state& state, Playing& playing, const glm::vec3& drawn, const me_now& me) {
    contact_watch& watch = playing.watch;
    const net_tuning& tuning = state.tuning.net;
    if (!playing.source) {
        return;
    }
    if (watch.predicted) {
        state.motion_sync.near_flight = true;
        if (prediction_overdue(*watch.flight, *watch.predicted, me.now_ms, tuning)) {
            watch.predicted.reset();
            watch.given_up = true;
            replay_with(state, playing, drawn, std::nullopt, watch.correction_before);
        }
        return;
    }
    if (!trajectory_near(playing.result, me.entry.position, me.reach)) {
        return;
    }
    const ball_flight& flight = flight_of(state, playing);
    if (flight_near(flight, me.entry.position, me.reach, me.now_ms)) {
        state.motion_sync.near_flight = true;
    }
    if (watch.given_up || playing.elapsed >= playing.result.duration) {
        return;
    }
    snapshot_entry mover = me.entry;
    mover.shooter = playing.source->account_id == me.entry.account;
    const shot_course flown = struck_course(playing.source->record, room_shot_course(state, *playing.source), state.tuning);
    const std::optional<flight_contact> found =
        first_contact(flight, logged_path(state.motion_sync.path, mover, me.now_ms), flown, state.tuning);
    if (!found) {
        return;
    }
    // My own shot ends with its flight: a prediction must have time to be retracted before.
    const std::int64_t contact_ms = step_clock_us(flight.launch_ms, found->step + 1) / micros_per_milli;
    const bool no_time_to_retract = mover.shooter && contact_ms + hold_ms(tuning) >= step_clock_us(flight.launch_ms, flight.steps) / micros_per_milli;
    if (prediction_overdue(flight, *found, me.now_ms, tuning)) {
        watch.given_up = true;
    } else if (!no_time_to_retract) {
        watch.predicted = found;
        watch.correction_before = playing.correction;
        replay_with(state, playing, drawn, found, glm::vec3(0.0f));
    }
}
}

void note_my_path(game_state& state, const std::int64_t time_ms) {
    if (!is_online(state)) {
        return;
    }
    const bool driving = driving_cart(state);
    const glm::vec3 at = driving ? state.cart.pose.position : state.player.position;
    note_path_sample(state.motion_sync.path, driving ? snapshot_kind::cart : snapshot_kind::person,
                     mover_sample{time_ms, at, driving ? state.cart.pose.yaw : 0.0f}, time_ms - hold_ms(state.tuning.net));
}

void update_contact_prediction(game_state& state) {
    state.motion_sync.near_flight = false;
    const bool passenger = state.ride && state.ride->owner != 0;
    if (!is_online(state) || state.online.status != net_status::connected || passenger) {
        return;
    }
    me_now me;
    me.now_ms = shot_unix_ms(state);
    note_my_path(state, me.now_ms);
    me.entry.account = state.online.account_id;
    me.entry.position = state.motion_sync.path.samples.empty() ? state.player.position : state.motion_sync.path.samples.back().position;
    me.reach = state.tuning.net.contact_send_meters / state.tuning.scale.meters_per_world_unit;
    if (state.shot && state.ball) {
        follow_flight(state, *state.shot, state.ball->position, me);
    }
    for (auto& [account, shot] : state.remote_shots) {
        follow_flight(state, shot, remote_shot_position(shot), me);
    }
}
