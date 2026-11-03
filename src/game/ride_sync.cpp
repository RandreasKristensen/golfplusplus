#include "game/ride_sync.h"

#include "game/game_state.h"
#include "game/golf_cart.h"
#include "game/mode_dispatch.h"
#include "game/play_area.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <utility>

namespace {
// A ride motion is carried on at most this long without a newer one: then
// the cart stands where it got to, as the driver has likely stopped sending.
constexpr float ride_extrapolation_seconds = 0.5f;

bool carries_passenger(const game_state& state) {
    const auto mine = state.online.carts.find(state.online.account_id);
    return mine != state.online.carts.end() && mine->second.passenger != 0;
}
}

void sync_ride_motion(game_state& state, const float dt) {
    state.motion_sync.ride_since += dt;
    if (!is_online(state) || !driving_cart(state) || !carries_passenger(state) ||
        state.motion_sync.ride_since < state.tuning.net.ride_motion_interval_seconds) {
        return;
    }
    // At the interval on average, whatever the frames: never a burst after a stall.
    const float interval = state.tuning.net.ride_motion_interval_seconds;
    state.motion_sync.ride_since = std::min(state.motion_sync.ride_since - interval, interval);
    net_command command;
    command.type = net_command_type::ride_motion;
    command.motion = current_motion(state);
    state.net_commands.push_back(std::move(command));
}

void follow_ride(game_state& state, const float dt) {
    const std::optional<room_ride>& ride = state.online.ride;
    const bool riding = state.ride && state.ride->owner != 0 && ride && ride->owner == state.ride->owner &&
        ride->motion.mode == motion_mode::cart;
    if (!is_online(state) || !riding) {
        state.ride_shown.reset();
        return;
    }
    const float since = state.online.server_now != 0
        ? std::clamp(server_seconds_between(ride->motion_at, state.online.server_now), 0.0f, ride_extrapolation_seconds)
        : 0.0f;
    const glm::vec3 seat = extrapolate_motion(ride->motion, since);
    const float yaw = wrap_angle(ride->motion.yaw + ride->motion.turn_rate * since);
    const cart_tuning& tuning = state.tuning.cart;
    if (!state.ride_shown || state.ride_shown->owner != ride->owner) {
        state.ride_shown = ride_view{ride->owner, ride->motion_at, cart_around_seat(seat, yaw, cart_seat::driver, tuning),
                                     glm::vec3(0.0f), 0.0f, 0.0f};
    }
    ride_view& view = *state.ride_shown;
    if (view.motion_at != ride->motion_at) {
        const glm::vec3 shown_seat = seat_position(view.shown, cart_seat::driver, tuning);
        view.correction = shown_seat - seat;
        view.correction.y = 0.0f;
        view.yaw_correction = wrap_angle(view.shown.yaw - yaw);
        view.correction_left = state.tuning.net.ride_correction_seconds;
        view.motion_at = ride->motion_at;
    }
    view.correction_left = std::max(0.0f, view.correction_left - dt);
    const float seconds = state.tuning.net.ride_correction_seconds;
    const float blend = seconds > 0.0f ? view.correction_left / seconds : 0.0f;
    const float shown_yaw = wrap_angle(yaw + view.yaw_correction * blend);
    cart_pose pose = cart_around_seat(seat + view.correction * blend, shown_yaw, cart_seat::driver, tuning);
    pose.position.y = terrain_height(state.area, pose.position);
    view.shown = pose;
}
