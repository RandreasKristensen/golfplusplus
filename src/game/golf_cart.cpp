#include "game/golf_cart.h"

#include "game/game_state.h"
#include "game/mode_dispatch.h"
#include "game/motion_sync.h"
#include "game/play_area.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>

namespace {
bool is_mine(const game_state& state, const std::uint64_t owner) {
    return owner == 0 || owner == state.online.account_id;
}

const room_cart* server_cart(const game_state& state, const std::uint64_t owner) {
    const auto cart = state.online.carts.find(owner);
    return cart != state.online.carts.end() ? &cart->second : nullptr;
}

// The server has me in the passenger seat of `owner`'s cart.
bool server_seats_me(const game_state& state, const std::uint64_t owner) {
    const room_cart* cart = server_cart(state, owner);
    return cart != nullptr && cart->passenger == state.online.account_id && state.online.account_id != 0;
}

void place_player(game_state& state, const glm::vec3& feet, const float yaw, frame_profile* profile) {
    state.player.position = feet;
    state.player.position.y = terrain_height(state.area, feet, profile);
    state.player.yaw = yaw;
}

// Out of the seat: I go on facing where my head was turned.
void end_ride(game_state& state) {
    state.player.yaw = view_yaw(state);
    state.look.head_yaw = 0.0f;
    state.ride.reset();
}

void push_cart_start(game_state& state) {
    audio_event event;
    event.type = audio_event_type::cart_start;
    state.audio_events.push_back(event);
}

void play_step(game_state& state, const cart_pose& cart, const float dt, frame_profile* profile) {
    cart_ride& ride = *state.ride;
    cart_step& step = *ride.step;
    const cart_tuning& tuning = state.tuning.cart;
    step.elapsed += dt;
    const float t = tuning.step_seconds > 0.0f ? clamp01(step.elapsed / tuning.step_seconds) : 1.0f;
    const float eased = t * t * (3.0f - 2.0f * t);
    const glm::vec3 seat = seat_position(cart, ride.seat, tuning);
    if (step.getting_in) {
        const float yaw = step.from_yaw + wrap_angle(cart.yaw - step.from_yaw) * eased;
        place_player(state, glm::mix(step.from, seat, eased), wrap_angle(yaw), profile);
    } else {
        place_player(state, glm::mix(seat, step_out_position(cart, ride.seat, tuning), eased), cart.yaw, profile);
    }
    if (t < 1.0f) {
        return;
    }
    if (!step.getting_in) {
        end_ride(state);
        return;
    }
    ride.step.reset();
    if (ride.owner == 0) {
        push_cart_start(state);
    }
}

void step_out(game_state& state) {
    cart_ride& ride = *state.ride;
    if (ride.owner == 0) {
        // Online the server parks the cart where its last motion has it.
        state.cart.velocity = 0.0f;
        state.cart.steer = 0.0f;
        state.player.speed = 0.0f;
        state.player.turn_rate = 0.0f;
        send_motion_now(state);
    } else {
        record_cart_left(state);
    }
    ride.step = cart_step{state.player.position, state.player.yaw, 0.0f, false};
}

void drive(game_state& state, const game_input& input, const float dt, frame_profile* profile) {
    const cart_tuning& tuning = state.tuning.cart;
    cart_state& cart = state.cart;
    const bool on_road = cart_on_road(state);
    const bool hub = in_hub(state);
    const float control = !hub ? 1.0f : (on_road ? tuning.road_control_scale : tuning.off_road_control_scale);
    const float road_speed = !hub ? 1.0f : (on_road ? tuning.road_speed_scale : tuning.off_road_speed_scale);

    // Steering turns a moving cart, the other way backing up.
    const float steer =
        tuning.full_steer_speed > 0.0f ? std::clamp(cart.velocity / tuning.full_steer_speed, -1.0f, 1.0f) : 1.0f;
    state.player.turn_rate = tuning.turn_rate * control * steer * strafe_direction(input);
    cart.steer += (strafe_direction(input) - cart.steer) * (1.0f - std::exp(-std::max(0.0f, tuning.steering_wheel_rate) * dt));
    cart.pose.yaw = wrap_angle(cart.pose.yaw + state.player.turn_rate * dt);

    const float throttle = walk_direction(input);
    const float target_speed = tuning.speed * road_speed * (throttle < 0.0f ? throttle * tuning.reverse_speed_scale : throttle);
    const float damping = std::max(0.0f, tuning.damping * control);
    cart.velocity += (target_speed - cart.velocity) * (1.0f - std::exp(-damping * dt));

    const glm::vec3 before = state.player.position;
    cart.pose.position += yaw_direction(cart.pose.yaw) * cart.velocity * dt;
    cart.pose.position.y = terrain_height(state.area, cart.pose.position, profile);
    place_player(state, seat_position(cart.pose, cart_seat::driver, tuning), cart.pose.yaw, profile);
    state.player.speed = cart.velocity;

    if (on_road && std::abs(cart.velocity) > still_speed) {
        const float meters = horizontal_distance(state.player.position, before) * state.tuning.scale.meters_per_world_unit;
        record_movement(state, state.rewards.cart_on_road, state.cart_meters_pending, meters);
    }
}

// The steering wheel of `owner`'s cart, from how their motion turns it.
float remote_steer(const game_state& state, const std::uint64_t owner) {
    const auto player = state.online.players.find(owner);
    if (player == state.online.players.end() || !player->second.motion || state.tuning.cart.turn_rate <= 0.0f) {
        return 0.0f;
    }
    const net_motion& motion = *player->second.motion;
    const float backing = motion.speed < 0.0f ? -1.0f : 1.0f;
    return std::clamp(motion.turn_rate / state.tuning.cart.turn_rate * backing, -1.0f, 1.0f);
}

// Sat beside another player, carried as they drive: my motion is theirs, so
// others carry me on with the cart.
void ride_along(game_state& state, const cart_pose& cart, frame_profile* profile) {
    place_player(state, seat_position(cart, state.ride->seat, state.tuning.cart), cart.yaw, profile);
    const auto owner = state.online.players.find(state.ride->owner);
    if (owner != state.online.players.end() && owner->second.motion && owner->second.motion->mode == motion_mode::cart) {
        state.player.speed = owner->second.motion->speed;
        state.player.turn_rate = owner->second.motion->turn_rate;
    }
}
}

void park_cart(game_state& state) {
    state.ride.reset();
    state.look.head_yaw = 0.0f;
    state.cart = cart_state{};
    if (!state.hub || state.hub->world.hole_starts.empty() || state.hub->markers.empty()) {
        return;
    }
    cart_pose pose = parked_cart_pose(state.hub->world.hole_starts.front().position, state.hub->markers.front().pin_position, 0,
                                      state.tuning.cart);
    pose.position = anchor_on_terrain(state.area, pose.position);
    state.cart.pose = pose;
}

bool on_foot(const game_state& state) {
    return !state.ride.has_value();
}

bool driving_cart(const game_state& state) {
    return state.ride && state.ride->owner == 0 && !state.ride->step;
}

std::optional<cart_pose> shown_cart(const game_state& state, const std::uint64_t owner) {
    if (is_mine(state, owner)) {
        return state.cart.pose;
    }
    const room_cart* cart = server_cart(state, owner);
    if (cart == nullptr) {
        return std::nullopt;
    }
    const auto driver = state.remote_avatars.find(owner);
    if (driver != state.remote_avatars.end() && driver->second.mode == motion_mode::cart) {
        return cart_around_seat(driver->second.position, driver->second.yaw, cart_seat::driver, state.tuning.cart);
    }
    return cart_pose{glm::vec3(cart->position.x, terrain_height(state.area, cart->position), cart->position.z), cart->yaw};
}

std::optional<std::uint64_t> cart_ridden_by(const game_state& state, const std::uint64_t account) {
    for (const auto& [owner, cart] : state.online.carts) {
        if (account != 0 && cart.passenger == account) {
            return owner;
        }
    }
    return std::nullopt;
}

std::optional<reachable_seat> nearest_seat(const game_state& state) {
    if (!on_foot(state) || state.mode != game_mode::walking) {
        return std::nullopt;
    }
    const cart_tuning& tuning = state.tuning.cart;
    std::optional<reachable_seat> best;
    const auto consider = [&](const std::uint64_t owner, const cart_pose& cart, const cart_seat seat) {
        const float distance = horizontal_distance(seat_position(cart, seat, tuning), state.player.position);
        if (in_seat_reach(cart, seat, state.player.position, tuning) && (!best || distance < best->distance)) {
            best = reachable_seat{owner, seat, distance};
        }
    };
    consider(0, state.cart.pose, cart_seat::driver);
    if (is_online(state)) {
        for (const auto& [owner, cart] : state.online.carts) {
            if (is_mine(state, owner) || cart.passenger != 0) {
                continue;
            }
            if (const std::optional<cart_pose> pose = shown_cart(state, owner)) {
                consider(owner, *pose, cart_seat::passenger);
            }
        }
    }
    return best;
}

void step_into_seat(game_state& state, const reachable_seat& seat) {
    if (seat.owner != 0) {
        // The server checks the reach from where it last had me.
        send_motion_now(state);
        record_cart_boarded(state, seat.owner);
    }
    state.ride = cart_ride{seat.owner, seat.seat, cart_step{state.player.position, state.player.yaw, 0.0f, true}, false};
}

void update_cart(game_state& state, const game_input& input, const float dt, frame_profile* profile) {
    if (!state.ride) {
        return;
    }
    const std::optional<cart_pose> cart = shown_cart(state, state.ride->owner);
    if (!cart) {
        leave_cart_at_once(state);
        return;
    }
    if (state.ride->step) {
        play_step(state, *cart, dt, profile);
    } else if (input.interact) {
        step_out(state);
    } else if (state.ride->owner == 0) {
        drive(state, input, dt, profile);
    } else {
        ride_along(state, *cart, profile);
    }
}

void leave_cart_at_once(game_state& state) {
    if (!state.ride) {
        return;
    }
    const cart_ride ride = *state.ride;
    if (ride.owner != 0 && server_seats_me(state, ride.owner)) {
        record_cart_left(state);
    }
    if (const std::optional<cart_pose> cart = shown_cart(state, ride.owner)) {
        place_player(state, step_out_position(*cart, ride.seat, state.tuning.cart), cart->yaw, nullptr);
    }
    end_ride(state);
    state.cart.velocity = 0.0f;
    state.cart.steer = 0.0f;
}

void follow_server_carts(game_state& state) {
    if (!is_online(state)) {
        return;
    }
    const room_cart* mine = server_cart(state, state.online.account_id);
    if (mine != nullptr && !state.cart.placed_by_server && !(state.ride && state.ride->owner == 0)) {
        state.cart.pose = cart_pose{anchor_on_terrain(state.area, mine->position), mine->yaw};
        state.cart.placed_by_server = true;
    }
    if (!state.ride || state.ride->owner == 0) {
        return;
    }
    cart_ride& ride = *state.ride;
    const bool stepping_out = ride.step && !ride.step->getting_in;
    if (server_seats_me(state, ride.owner)) {
        ride.confirmed = true;
    } else if (ride.confirmed && !stepping_out) {
        leave_cart_at_once(state);
    }
}

std::vector<cart_in_world> carts_in_world(const game_state& state) {
    std::vector<cart_in_world> carts;
    if (!state.hub) {
        return carts;
    }
    carts.push_back(cart_in_world{state.cart.pose, driving_cart(state), state.cart.steer});
    if (!is_online(state)) {
        return carts;
    }
    for (const auto& [owner, cart] : state.online.carts) {
        if (is_mine(state, owner)) {
            continue;
        }
        const auto driver = state.remote_avatars.find(owner);
        const bool driven = driver != state.remote_avatars.end() && driver->second.mode == motion_mode::cart;
        if (const std::optional<cart_pose> pose = shown_cart(state, owner)) {
            carts.push_back(cart_in_world{*pose, driven, driven ? remote_steer(state, owner) : 0.0f});
        }
    }
    return carts;
}
