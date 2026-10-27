#include "game/golf_cart.h"

#include "game/body_contact.h"
#include "game/game_state.h"
#include "game/mode_dispatch.h"
#include "game/motion_sync.h"
#include "game/play_area.h"
#include "game/world_sounds.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/trigonometric.hpp>

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

cart_motion stopped_motion(cart_motion motion) {
    motion.velocity = glm::vec2(0.0f);
    motion.yaw_rate = 0.0f;
    motion.lift_seconds = 0.0f;
    motion.load = 0.0f;
    return motion;
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
        push_audio_event(state, audio_event_type::cart_start, cart.position);
    }
}

void step_out(game_state& state) {
    cart_ride& ride = *state.ride;
    if (ride.owner == 0) {
        // Online the server parks the cart where its last motion has it.
        state.cart.motion = stopped_motion(state.cart.motion);
        state.cart.steer = 0.0f;
        state.player.speed = 0.0f;
        state.player.turn_rate = 0.0f;
        send_motion_now(state);
    } else {
        record_cart_left(state);
    }
    ride.step = cart_step{state.player.position, state.player.yaw, 0.0f, false};
}

// Tipped or sunk under me: online the server hears where and how fast it
// went over, and I tumble or wade out beside it.
void overturned(game_state& state, const cart_motion& before) {
    const cart_condition condition = state.cart.motion.condition;
    state.player.speed = cart_forward_speed(before) * state.tuning.cart.time_scale / state.tuning.scale.meters_per_world_unit;
    state.player.turn_rate = before.yaw_rate * state.tuning.cart.time_scale;
    send_motion_now(state);
    record_cart_overturned(state, condition);
    push_audio_event(state, condition == cart_condition::sunk ? audio_event_type::cart_sink : audio_event_type::cart_tip,
                     state.cart.pose.position);
    state.cart.sunk_seconds = 0.0f;
    leave_cart_at_once(state);
}

// One fixed step of my cart: over the ground, clear of what stands in its
// way, on the surface or falling onto it. Its physics steps
// cart.time_scale times as much cart time as the frame's (a real cart,
// faster); falling off an edge is in the frame's own time.
void step_my_cart(game_state& state, const cart_controls& controls, const cart_spec& spec, const float grip_scale,
                  const std::vector<solid>& others, frame_profile* profile) {
    cart_state& cart = state.cart;
    cart.motion.yaw = cart.pose.yaw;
    cart_ground ground = cart_ground_at(state.area, state.world.cart_roads, state.tuning, state.weather.wetness,
                                        cart.pose.position, profile);
    ground.grip *= grip_scale;
    ground.on_ground = cart.vertical.grounded;
    const cart_motion before = cart.motion;
    const float cart_seconds = cart_step_seconds * state.tuning.cart.time_scale;
    cart.motion = step_cart(cart.motion, controls, ground, spec, cart_seconds);
    cart.pose.yaw = cart.motion.yaw;
    if (cart.motion.condition != cart_condition::upright) {
        overturned(state, before);
        return;
    }
    const glm::vec3 from = cart.pose.position;
    const glm::vec2 travel = cart.motion.velocity * (cart_seconds / state.tuning.scale.meters_per_world_unit);
    cart.pose.position += glm::vec3(travel.x, 0.0f, travel.y);
    keep_cart_clear(state, from, others);
    settle_cart(state, cart_step_seconds, profile);
}

void drive(game_state& state, const game_input& input, const float dt, frame_profile* profile) {
    const cart_tuning& tuning = state.tuning.cart;
    cart_state& cart = state.cart;
    const cart_spec spec = my_cart_spec(state);
    const float grip_scale = effective_value(1.0f, effect_target_cart_drift, current_effects(state), shot_unix_ms(state));
    const std::vector<solid> others = cart_obstacles(state);
    const glm::vec3 before = state.player.position;
    const bool on_road = cart_on_road(state);
    // The driver turns the wheel in the cart's time too, so it steers a real cart's line.
    const float wheel_rate =
        1.0f - std::exp(-std::max(0.0f, tuning.steering_wheel_rate) * cart_step_seconds * tuning.time_scale);
    cart.unstepped_seconds += dt;
    // Within a hair of a whole step counts as one: frame times summed in
    // floats fall just short of it.
    constexpr float step_rounding = 1.0e-5f;
    while (cart.unstepped_seconds >= cart_step_seconds - step_rounding && driving_cart(state)) {
        cart.unstepped_seconds -= cart_step_seconds;
        cart.steer += (strafe_direction(input) - cart.steer) * wheel_rate;
        step_my_cart(state, cart_controls{walk_direction(input), cart.steer}, spec, grip_scale, others, profile);
    }
    if (!driving_cart(state)) {
        return;
    }
    place_player(state, seat_position(cart.pose, cart_seat::driver, tuning), cart.pose.yaw, profile);
    state.player.speed = my_cart_speed(state);
    state.player.turn_rate = cart.motion.yaw_rate * tuning.time_scale;

    if (on_road && std::abs(state.player.speed) > still_speed) {
        const float meters = horizontal_distance(state.player.position, before) * state.tuning.scale.meters_per_world_unit;
        record_movement(state, state.rewards.cart_on_road, state.cart_meters_pending, meters);
    }
}

// The steering wheel of `owner`'s cart, `item`, from how their motion turns
// it: the front wheels' turn that bends its path as tightly.
float remote_steer(const game_state& state, const std::uint64_t owner, const std::string& item) {
    const auto player = state.online.players.find(owner);
    if (player == state.online.players.end() || !player->second.motion) {
        return 0.0f;
    }
    const net_motion& motion = *player->second.motion;
    const cart_spec& spec = cart_spec_of(state, item);
    if (std::abs(motion.speed) < still_speed || spec.max_steer_degrees <= 0.0f) {
        return 0.0f;
    }
    const float wheels = std::atan(motion.turn_rate * spec.wheelbase_m / motion.speed);
    return std::clamp(wheels / glm::radians(spec.max_steer_degrees), -1.0f, 1.0f);
}

// The cart item `owner` (another player) has equipped, as their row has it.
std::string others_cart_item(const game_state& state, const std::uint64_t owner) {
    const auto equipped = state.online.loadouts.find(owner);
    return equipped != state.online.loadouts.end() ? fitted_loadout(equipped->second, state.default_loadout, state.items).cart
                                                   : state.default_loadout.cart;
}

// Whether `owner`'s cart stands on its wheels.
cart_condition condition_of(const game_state& state, const std::uint64_t owner) {
    if (is_mine(state, owner)) {
        return state.cart.motion.condition;
    }
    const room_cart* cart = server_cart(state, owner);
    return cart != nullptr ? cart->condition : cart_condition::upright;
}

// Back on its wheels where it lay, still.
void stand_up(cart_state& cart) {
    cart.motion = stopped_motion(cart.motion);
    cart.motion.condition = cart_condition::upright;
    cart.vertical = vertical_body{cart.pose.position.y, 0.0f, true};
}

// My cart parked at slot 0 beside hole 1 (or nowhere before a course starts).
void park_at_hole_one(game_state& state) {
    state.cart = cart_state{};
    if (state.world.hole_starts.empty() || state.holes.empty()) {
        return;
    }
    cart_pose pose = parked_cart_pose(state.world.hole_starts.front().position, state.holes.front().pin_position, 0,
                                      state.tuning.cart);
    pose.position = anchor_on_terrain(state.area, pose.position);
    state.cart.pose = pose;
    state.cart.vertical.y = pose.position.y;
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
    state.righting.reset();
    state.look.head_yaw = 0.0f;
    park_at_hole_one(state);
}

bool on_foot(const game_state& state) {
    return !state.ride.has_value();
}

bool driving_cart(const game_state& state) {
    return state.ride && state.ride->owner == 0 && !state.ride->step;
}

const cart_spec& cart_spec_of(const game_state& state, const std::string& item) {
    const item_definition* found = state.items.find(item);
    if (found == nullptr || !found->cart) {
        found = state.items.find(state.default_loadout.cart);  // the loader made sure it is a cart
    }
    return *found->cart;
}

cart_spec my_cart_spec(const game_state& state) {
    cart_spec spec = cart_spec_of(state, equipped_loadout(state).cart);
    const std::vector<active_effect> effects = current_effects(state);
    const std::int64_t now = shot_unix_ms(state);
    spec.top_speed_mps = effective_value(spec.top_speed_mps, effect_target_cart_top_speed, effects, now);
    spec.max_drive_force_n = effective_value(spec.max_drive_force_n, effect_target_cart_uphill, effects, now);
    return spec;
}

float my_cart_speed(const game_state& state) {
    return cart_forward_speed(state.cart.motion) * state.tuning.cart.time_scale / state.tuning.scale.meters_per_world_unit;
}

float my_cart_top_speed(const game_state& state) {
    return game_top_speed(my_cart_spec(state), state.tuning.cart) / state.tuning.scale.meters_per_world_unit;
}

float motor_pitch(const game_state& state) {
    const float top_speed = std::max(my_cart_spec(state).top_speed_mps, 0.1f);
    return motor_pitch_at(state.tuning.cart, std::abs(cart_forward_speed(state.cart.motion)) / top_speed, state.cart.motion.load);
}

float motor_pitch_at(const cart_tuning& tuning, const float speed_share, const float load) {
    return tuning.motor_idle_pitch + (tuning.motor_top_speed_pitch - tuning.motor_idle_pitch) * clamp01(speed_share) +
        tuning.motor_load_pitch * clamp01(load);
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
    if (state.cart.motion.condition == cart_condition::upright) {
        consider(0, state.cart.pose, cart_seat::driver);
    }
    if (is_online(state)) {
        for (const auto& [owner, cart] : state.online.carts) {
            if (is_mine(state, owner) || cart.passenger != 0 || cart.condition != cart_condition::upright) {
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

std::optional<reachable_tipped_cart> nearest_tipped_cart(const game_state& state) {
    if (!on_foot(state) || state.mode != game_mode::walking || state.righting) {
        return std::nullopt;
    }
    std::optional<reachable_tipped_cart> best;
    const auto consider = [&](const std::uint64_t owner, const cart_pose& cart) {
        const float distance = horizontal_distance(cart.position, state.player.position);
        if (distance <= state.tuning.cart.board_reach && (!best || distance < best->distance)) {
            best = reachable_tipped_cart{owner, distance};
        }
    };
    if (cart_tipped(state.cart.motion.condition)) {
        consider(0, state.cart.pose);
    }
    if (is_online(state)) {
        for (const auto& [owner, cart] : state.online.carts) {
            if (!is_mine(state, owner) && cart_tipped(cart.condition)) {
                consider(owner, cart_pose{cart.position, cart.yaw});
            }
        }
    }
    return best;
}

void start_righting(game_state& state, const reachable_tipped_cart& cart) {
    // Online the server checks the reach from where it last had me.
    send_motion_now(state);
    state.righting = cart_righting{cart.owner, 0.0f};
}

void update_carts(game_state& state, const float dt) {
    if (state.righting) {
        const std::uint64_t owner = state.righting->owner;
        const std::optional<cart_pose> cart = shown_cart(state, owner);
        const bool beside = cart && on_foot(state) && state.mode == game_mode::walking &&
            horizontal_distance(cart->position, state.player.position) <= state.tuning.cart.board_reach;
        const std::string item = is_mine(state, owner) ? equipped_loadout(state).cart : others_cart_item(state, owner);
        if (!beside || !cart_tipped(condition_of(state, owner))) {
            state.righting.reset();
        } else if ((state.righting->elapsed += dt) >= cart_spec_of(state, item).right_seconds) {
            state.righting.reset();
            record_cart_righted(state, owner);
            if (is_mine(state, owner)) {
                stand_up(state.cart);
            }
            push_audio_event(state, audio_event_type::cart_righted, cart->position);
        }
    }
    // Online the server tows it (follow_server_carts).
    if (!is_online(state) && state.cart.motion.condition == cart_condition::sunk) {
        state.cart.sunk_seconds += dt;
        if (state.cart.sunk_seconds >= my_cart_spec(state).tow_seconds) {
            park_at_hole_one(state);
        }
    }
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
    if (ride.owner == 0) {
        state.cart.motion = stopped_motion(state.cart.motion);
        state.cart.steer = 0.0f;
    }
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
    // Righted by someone, or towed back to where the server parked it.
    if (mine != nullptr && mine->condition != state.cart.server_condition) {
        state.cart.server_condition = mine->condition;
        if (mine->condition == cart_condition::upright && state.cart.motion.condition != cart_condition::upright) {
            if (state.cart.motion.condition == cart_condition::sunk) {
                state.cart.pose = cart_pose{anchor_on_terrain(state.area, mine->position), mine->yaw};
            }
            stand_up(state.cart);
        }
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

void adopt_server_cart(game_state& state) {
    const room_cart* mine = server_cart(state, state.online.account_id);
    if (mine == nullptr || mine->condition == state.cart.motion.condition) {
        return;
    }
    if (mine->condition == cart_condition::upright) {
        stand_up(state.cart);
        return;
    }
    if (driving_cart(state)) {
        leave_cart_at_once(state);
    }
    state.cart.motion = stopped_motion(state.cart.motion);
    state.cart.motion.condition = mine->condition;
}

ground_body cart_body(const cart_pose& pose, const cart_tuning& tuning) {
    const glm::vec3 forward = yaw_direction(pose.yaw);
    return box_body(glm::vec2(forward.x, forward.z), 0.5f * (tuning.body_front + tuning.body_back), tuning.body_half_width,
                    0.5f * (tuning.body_front - tuning.body_back), tuning.scrape_friction, tuning.step_up);
}

std::vector<cart_in_world> carts_in_world(const game_state& state) {
    std::vector<cart_in_world> carts;
    if (!course_started(state)) {
        return carts;
    }
    carts.push_back(cart_in_world{0, state.cart.pose, state.cart.motion.condition, driving_cart(state), state.cart.steer,
                                  equipped_loadout(state).cart, my_cart_speed(state)});
    if (!is_online(state)) {
        return carts;
    }
    for (const auto& [owner, cart] : state.online.carts) {
        if (is_mine(state, owner)) {
            continue;
        }
        const auto driver = state.remote_avatars.find(owner);
        const bool driven = driver != state.remote_avatars.end() && driver->second.mode == motion_mode::cart &&
            cart.condition == cart_condition::upright;
        const std::string item = others_cart_item(state, owner);
        const auto player = state.online.players.find(owner);
        const float speed = driven && player != state.online.players.end() && player->second.motion ? player->second.motion->speed : 0.0f;
        if (const std::optional<cart_pose> pose = shown_cart(state, owner)) {
            carts.push_back(cart_in_world{owner, *pose, cart.condition, driven, driven ? remote_steer(state, owner, item) : 0.0f,
                                          item, speed});
        }
    }
    return carts;
}
