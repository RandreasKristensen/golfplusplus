#include "game/hub_flow.h"
#include "game/mode_dispatch.h"
#include "game/course_session.h"
#include "game/golf_bag.h"
#include "game/hub_scene.h"
#include "game/motion_sync.h"
#include "game/tee_times.h"
#include "game/text_ids.h"
#include "physics/vector_math.h"

bool course_warmup(const game_state& state) {
    const auto me = my_room_player(state);
    return state.kind == place_kind::course && (is_online(state) ? me && !me->round_started : state.offline_preparing);
}
bool first_tee_required(const game_state& state) {
    return is_online(state) && state.kind == place_kind::course && state.online.reservation &&
        !state.online.reservation->completed && state.online.reservation->started_at == 0;
}
bool reservation_expired(const game_state& state) {
    const auto& booking = active_booking(state);
    if (!is_online(state) || !booking) { return !booking; }
    // As the server's tee_missed: in the hub the bus has gone at the tee time.
    const std::int64_t due = booking->scheduled_at +
        (state.kind == place_kind::hub ? 0 : state.tuning.server.tee_expiry_minutes * minute_micros);
    return booking->started_at == 0 && state.online.server_now >= due;
}
void request_ball_preparation(game_state& state, const net_command_type type, const std::size_t hole, const bool give_up) {
    if (!is_online(state)) {
        if (!state.offline_preparing || !state.round || hole >= state.holes.size()) { return; }
        if (type == net_command_type::start_booked_round) {
            state.offline_preparing = false;
            state.ball.reset();
            tee_up(state, 0, false);
            return;
        }
        const auto station = warmup_position(state.area, hole, state.tuning);
        state.ball = my_ball{hole, resting_ball_position(state.area, station, state.tuning.scale.ball_physics_radius_meters),
            0, ball_place::teed, ball_kind::warmup};
        state.shot.reset(); state.address = address_state{}; state.mode = game_mode::walking;
        state.flight_path_points.clear(); state.give_up_prompt.reset(); state.ball_correction.reset();
        state.player.position = tee_stance_position(state.area, state.ball->position, state.area.cups[hole].centre,
            state.tuning.player.ball_stand_off_distance);
        state.player.yaw = yaw_towards(state.ball->position, state.area.cups[hole].centre);
        select_putter(state);
        return;
    }
    if (state.hub_flow.ball_request || !state.online.room_ready) { return; }
    // Before the tee time only the server knows whether the first tee is clear.
    if (type == net_command_type::start_booked_round &&
        (!state.online.reservation || state.online.reservation->completed || reservation_expired(state))) {
        state.notice = game_notice{!state.online.reservation ? text_online_error_booking_required :
            text_online_error_booking_expired, 0}; return;
    }
    state.hub_flow.ball_request = type;
    state.hub_flow.ball_updates = state.online.ball_updates;
    net_command command;
    command.type = type;
    command.hole_index = hole;
    command.give_up = give_up;
    request_tee_action(state, command);
}
void clear_online_presentation(game_state& state) {
    state.dap_pending.reset(); state.online.daps.clear();
    state.ball.reset(); state.shot.reset(); state.address = address_state{};
    state.mode = game_mode::walking; state.ball_correction.reset();
    state.flight_path_points.clear(); state.remote_avatars.clear(); state.remote_shots.clear();
    state.picker.reset(); state.handling.reset(); state.give_up_prompt.reset();
    state.pacing.reset(); state.ride.reset(); state.ride_shown.reset(); state.cart_falls.clear();
    state.online_hole = online_hole_state{};
    state.shot_number.reset(); state.smoking.reset(); state.drinking.reset();
    state.audio_events.clear();
    reset_motion_sync(state);
}
void select_putter(game_state& state) {
    for (std::size_t i = 0; i < state.clubs.size(); ++i) {
        if (state.clubs[i].club_class == "putter") { state.selected_club = i; return; }
    }
}
void request_bus_travel(game_state& state, const bool takes_cart) {
    if (!state.hub_flow.travel_course.empty() || (is_online(state) && !state.online.room_ready)) { return; }
    const auto& booking = active_booking(state);
    net_command command;
    command.type = state.kind == place_kind::hub ? net_command_type::take_bus : net_command_type::return_to_hub;
    command.takes_cart = takes_cart;
    if (command.type == net_command_type::take_bus && !booking) {
        state.notice = game_notice{text_online_error_booking_required, 0}; return;
    }
    state.takes_cart = takes_cart;
    state.hub_flow.travel_course = command.type == net_command_type::take_bus ? booking->course_id : online_hub_course;
    request_tee_action(state, command);
    state.hub_flow.screen = hub_screen::none;
    clear_online_presentation(state);
}
