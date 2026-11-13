#include "game/golf_cart.h"

#include "game/body_contact.h"
#include "game/game_state.h"
#include "game/mode_dispatch.h"
#include "game/motion_sync.h"
#include "game/movement_step.h"
#include "game/play_area.h"
#include "game/text_ids.h"
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

float eased(const float t) {
    return t * t * (3.0f - 2.0f * t);
}

void play_step(game_state& state, const cart_pose& cart, const float dt, frame_profile* profile) {
    cart_ride& ride = *state.ride;
    cart_step& step = *ride.step;
    const cart_tuning& tuning = state.tuning.cart;
    step.elapsed += dt;
    const bool scoots = step.getting_in && step.entered != ride.seat;
    const float seconds = tuning.step_seconds + (scoots ? tuning.scoot_seconds : 0.0f);
    const float t = tuning.step_seconds > 0.0f ? clamp01(step.elapsed / tuning.step_seconds) : 1.0f;
    const glm::vec3 seat = seat_position(cart, ride.seat, tuning);
    if (step.getting_in) {
        // Into the seat by the door, then across to mine.
        const glm::vec3 door_seat = seat_position(cart, step.entered, tuning);
        const float yaw = step.from_yaw + wrap_angle(cart.yaw - step.from_yaw) * eased(t);
        const float across = scoots && tuning.scoot_seconds > 0.0f
            ? clamp01((step.elapsed - tuning.step_seconds) / tuning.scoot_seconds)
            : 0.0f;
        const glm::vec3 at = t < 1.0f ? glm::mix(step.from, door_seat, eased(t)) : glm::mix(door_seat, seat, eased(across));
        place_player(state, at, wrap_angle(yaw), profile);
    } else {
        place_player(state, glm::mix(seat, step_out_position(cart, ride.seat, tuning), eased(t)), cart.yaw, profile);
    }
    if (step.elapsed < seconds) {
        return;
    }
    if (!step.getting_in) {
        end_ride(state);
        return;
    }
    if (is_online(state) && !ride.confirmed) {
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
        if (is_online(state)) {
            net_command command;
            const auto row = state.online.carts.find(state.online.account_id);
            if (row != state.online.carts.end()) {
                state.cart.exit_correction = state.cart.pose.position - row->second.position;
                state.cart.exit_yaw_correction = wrap_angle(state.cart.pose.yaw - row->second.yaw);
                state.cart.exit_correction_left = state.tuning.net.remote_correction_seconds;
            }
            command.type = net_command_type::leave_wheel;
            state.net_commands.push_back(command);
        }
    } else {
        record_cart_left(state);
    }
    ride.step = cart_step{state.player.position, state.player.yaw, 0.0f, false};
}

// Tipped or sunk under me: I tumble or wade out beside it. Online the
// server's own steps tip or sink it too; my steps up to it go out first.
void overturned(game_state& state, const cart_motion& before) {
    const cart_condition condition = state.cart.motion.condition;
    state.player.speed = cart_forward_speed(before) * state.tuning.cart.time_scale / state.tuning.scale.meters_per_world_unit;
    state.player.turn_rate = before.yaw_rate * state.tuning.cart.time_scale;
    send_motion_now(state);
    push_audio_event(state, condition == cart_condition::sunk ? audio_event_type::cart_sink : audio_event_type::cart_tip,
                     state.cart.pose.position);
    state.cart.sunk_seconds = 0.0f;
    leave_cart_at_once(state);
}

void drive(game_state& state, const game_input& input, const float dt, frame_profile* profile) {
    const cart_tuning& tuning = state.tuning.cart;
    cart_state& cart = state.cart;
    const cart_spec spec = my_cart_spec(state);
    const float grip_scale = cart_grip_scale(current_effects(state), shot_unix_ms(state));
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
        move_input step;
        step.kind = move_kind::cart;
        step.forward = static_cast<std::int8_t>(walk_direction(input));
        step.steer = quantise_steer(cart.steer);
        const cart_controls controls{static_cast<float>(step.forward), steer_of(step.steer)};
        const std::uint64_t number = record_step(state, step);
        if (is_online(state)) {
            state.motion_sync.cart_steps.push_back(predicted_cart_step{number, controls, spec, grip_scale, state.weather.wetness});
        }
        const cart_motion before_step = cart.motion;
        step_predicted_cart(state, controls, spec, grip_scale, others, profile);
        if (cart.motion.condition != cart_condition::upright) {
            overturned(state, before_step);
        }
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

// Whether `owner`'s driver sits at its wheel (another's cart, as shown).
bool driver_seated(const game_state& state, const std::uint64_t owner) {
    const auto driver = state.remote_avatars.find(owner);
    const room_cart* row = server_cart(state, owner);
    return row != nullptr && row->driver != 0 && driver != state.remote_avatars.end() && driver->second.mode == motion_mode::cart;
}

// Into the seat `door` leads to, or a notice that it is taken.
void step_in_at(game_state& state, const seat_door& door) {
    const room_cart* row = door.owner != 0 ? server_cart(state, door.owner)
                                           : (is_online(state) ? server_cart(state, state.online.account_id) : nullptr);
    // Through the passenger's door with someone sat there, or through the
    // driver's at another's cart with them at the wheel: no way past.
    const bool passenger_sat = row != nullptr && row->passenger != 0;
    const bool blocked = door.owner == 0 ? door.door == cart_seat::passenger && passenger_sat
                                         : passenger_sat || (door.door == cart_seat::driver && driver_seated(state, door.owner));
    if (blocked) {
        state.notice = game_notice{text_hud_seat_taken, 0.0f};
        return;
    }
    if (is_online(state)) {
        send_motion_now(state);
        if (door.owner == 0) {
            adopt_server_cart(state);
            net_command command;
            command.type = net_command_type::take_wheel;
            state.net_commands.push_back(command);
        } else {
            record_cart_boarded(state, door.owner);
        }
    }
    state.ride = cart_ride{door.owner, door.seat, cart_step{state.player.position, state.player.yaw, 0.0f, true, door.door}, false};
}

// Going over, on my side or another's: a fall starts when it is seen
// tipping, already lying when first seen so; stepped in fixed steps.
void update_fall(game_state& state, const std::uint64_t owner, const cart_condition condition, const cart_spec& spec,
                 const float dt) {
    const auto known = state.cart_falls.find(owner);
    cart_fall_shown& shown = state.cart_falls[owner];
    if (known == state.cart_falls.end() && cart_tipped(condition)) {
        shown.fall = cart_fall{cart_lying_angle, 0.0f, true};
    } else if (cart_tipped(condition) && !cart_tipped(shown.seen)) {
        shown.fall = start_cart_fall(spec);
        shown.unstepped = 0.0f;
    }
    shown.seen = condition;
    if (!cart_tipped(condition)) {
        shown.fall = cart_fall{};
        return;
    }
    shown.unstepped += dt;
    while (shown.unstepped >= cart_fall_step_seconds && !shown.fall.resting) {
        shown.unstepped -= cart_fall_step_seconds;
        shown.fall = step_cart_fall(shown.fall, spec, state.tuning.cart.fall_restitution, cart_fall_step_seconds);
    }
}

float fall_angle_of(const game_state& state, const std::uint64_t owner) {
    const auto fall = state.cart_falls.find(owner);
    return fall != state.cart_falls.end() ? fall->second.fall.angle : cart_lying_angle;
}

void update_falls(game_state& state, const float dt) {
    if (own_cart(state)) {
        update_fall(state, 0, state.cart.motion.condition, my_cart_spec(state), dt);
    }
    if (is_online(state)) {
        for (const auto& [owner, cart] : state.online.carts) {
            if (!is_mine(state, owner)) {
                update_fall(state, owner, cart.condition, cart_spec_of(state, others_cart_item(state, owner)), dt);
            }
        }
    }
    for (auto it = state.cart_falls.begin(); it != state.cart_falls.end();) {
        const bool gone = it->first == 0 ? !own_cart(state) : state.online.carts.count(it->first) == 0;
        it = gone ? state.cart_falls.erase(it) : std::next(it);
    }
}
}

void park_cart(game_state& state) {
    state.ride.reset();
    state.righting.reset();
    state.look.head_yaw = 0.0f;
    reset_motion_sync(state);
    if (is_online(state)) {
        state.cart = cart_state{};
        adopt_server_cart(state);
    } else {
        park_at_hole_one(state);
    }
}

bool own_cart(const game_state& state) {
    return state.place.carts && state.takes_cart &&
        (!is_online(state) || server_cart(state, state.online.account_id) != nullptr);
}

bool on_foot(const game_state& state) {
    return !state.ride.has_value();
}

bool driving_cart(const game_state& state) {
    return state.ride && state.ride->owner == 0 && !state.ride->step &&
        (!is_online(state) || state.ride->confirmed);
}

const cart_spec& cart_spec_of(const game_state& state, const std::string& item) {
    const item_definition* found = state.items.find(item);
    if (found == nullptr || !found->cart) {
        found = state.items.find(state.default_loadout.cart);  // the loader made sure it is a cart
    }
    return *found->cart;
}

cart_spec my_cart_spec(const game_state& state) {
    return effective_cart_spec(cart_spec_of(state, equipped_loadout(state).cart), current_effects(state), shot_unix_ms(state));
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
        if (!own_cart(state)) {
            return std::nullopt;
        }
        if (!is_online(state) || driving_cart(state)) {
            return state.cart.pose;
        }
        const room_cart& row = state.online.carts.at(state.online.account_id);
        return cart_pose{row.position, row.yaw};
    }
    const room_cart* cart = server_cart(state, owner);
    if (cart == nullptr) {
        return std::nullopt;
    }
    // The cart I ride in, as its driver's ride motions carry it.
    if (state.ride_shown && state.ride_shown->owner == owner && cart->driver != 0 && cart->condition == cart_condition::upright) {
        return state.ride_shown->shown;
    }
    const auto driver = state.remote_avatars.find(owner);
    if (cart->driver != 0 && cart->condition == cart_condition::upright &&
        driver != state.remote_avatars.end() && driver->second.mode == motion_mode::cart) {
        return cart_around_seat(driver->second.position, driver->second.yaw, cart_seat::driver, state.tuning.cart);
    }
    return cart_pose{cart->position, cart->yaw};
}

std::optional<std::uint64_t> cart_ridden_by(const game_state& state, const std::uint64_t account) {
    for (const auto& [owner, cart] : state.online.carts) {
        if (account != 0 && cart.passenger == account) {
            return owner;
        }
    }
    return std::nullopt;
}

cart_condition cart_condition_of(const game_state& state, const std::uint64_t owner) {
    if (is_mine(state, owner) && (!is_online(state) || driving_cart(state))) {
        return state.cart.motion.condition;
    }
    const room_cart* cart = server_cart(state, is_mine(state, owner) ? state.online.account_id : owner);
    return cart != nullptr ? cart->condition : cart_condition::upright;
}

std::optional<seat_door> seat_door_at(const game_state& state) {
    if (!on_foot(state) || state.mode != game_mode::walking || state.handling) {
        return std::nullopt;
    }
    const cart_tuning& tuning = state.tuning.cart;
    std::optional<seat_door> found;
    float best = 0.0f;
    const auto consider = [&](const std::uint64_t owner, const cart_pose& cart, const cart_seat seat) {
        for (const cart_seat door : {cart_seat::driver, cart_seat::passenger}) {
            const float distance = horizontal_distance(seat_position(cart, door, tuning), state.player.position);
            if (at_seat_door(cart, door, state.player.position, tuning) && (!found || distance < best)) {
                found = seat_door{owner, door, seat};
                best = distance;
            }
        }
    };
    if (const auto mine = shown_cart(state, 0); mine && cart_condition_of(state, 0) == cart_condition::upright) {
        consider(0, *mine, cart_seat::driver);
    }
    if (is_online(state)) {
        for (const auto& [owner, cart] : state.online.carts) {
            if (is_mine(state, owner) || cart.condition != cart_condition::upright) {
                continue;
            }
            if (const std::optional<cart_pose> pose = shown_cart(state, owner)) {
                consider(owner, *pose, cart_seat::passenger);
            }
        }
    }
    return found;
}

void walk_into_seat(game_state& state, const game_input& input, const float dt) {
    const std::optional<seat_door> door = seat_door_at(state);
    const bool walking = walk_direction(input) != 0.0f || strafe_direction(input) != 0.0f;
    if (!door || !walking) {
        state.door_push.reset();
        return;
    }
    const std::optional<cart_pose> cart = shown_cart(state, door->owner);
    // The way I walk, against the way into the cart.
    const float sign = state.player.speed < 0.0f ? -1.0f : 1.0f;
    const glm::vec3 going = yaw_direction(state.player.yaw + state.player.move_angle) * sign;
    const glm::vec3 inward = -seat_side_direction(*cart, door->door);
    if (going.x * inward.x + going.z * inward.z < state.tuning.cart.door_push_share) {
        state.door_push.reset();
        return;
    }
    const bool same = state.door_push && state.door_push->door.owner == door->owner && state.door_push->door.door == door->door;
    if (!same) {
        state.door_push = door_push{*door, 0.0f};
    }
    state.door_push->seconds += dt;
    if (state.door_push->seconds >= state.tuning.cart.door_push_seconds) {
        state.door_push.reset();
        step_in_at(state, *door);
    }
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
    if (const auto mine = shown_cart(state, 0); mine && cart_tipped(cart_condition_of(state, 0))) {
        consider(0, *mine);
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
    state.cart.exit_correction_left = std::max(0.0f, state.cart.exit_correction_left - dt);
    update_falls(state, dt);
    if (state.righting) {
        const std::uint64_t owner = state.righting->owner;
        const std::optional<cart_pose> cart = shown_cart(state, owner);
        const bool beside = cart && on_foot(state) && state.mode == game_mode::walking &&
            horizontal_distance(cart->position, state.player.position) <= state.tuning.cart.board_reach;
        const std::string item = is_mine(state, owner) ? equipped_loadout(state).cart : others_cart_item(state, owner);
        if (!beside || !cart_tipped(cart_condition_of(state, owner))) {
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
            // My bag left strapped on it is set down where it went in, as the server does.
            if (state.bag.where == bag_where::on_cart && state.bag.cart_owner == 0) {
                const glm::vec3 at = step_out_position(state.cart.pose, state.bag.seat, state.tuning.cart);
                state.bag = bag_place{bag_where::on_ground, cart_seat::driver, 0, anchor_on_terrain(state.area, at),
                                      state.cart.pose.yaw};
            }
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
        if (driving_cart(state)) {
            drive(state, input, dt, profile);
        } else {
            place_player(state, seat_position(*cart, cart_seat::driver, state.tuning.cart), cart->yaw, profile);
        }
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
    if (state.ride && state.ride->owner == 0) {
        cart_ride& ride = *state.ride;
        const bool stepping_out = ride.step && !ride.step->getting_in;
        if (mine && mine->driver == state.online.account_id) {
            ride.confirmed = true;
        } else if ((ride.confirmed || !mine) && !stepping_out) {
            adopt_server_cart(state);
            leave_cart_at_once(state);
        }
    }
    if (!driving_cart(state)) {
        adopt_server_cart(state);
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
    if (mine == nullptr) {
        return;
    }
    state.cart.pose = cart_pose{mine->position, mine->yaw};
    state.cart.motion = cart_motion{};
    state.cart.motion.yaw = mine->yaw;
    state.cart.motion.condition = mine->condition;
    state.cart.vertical = vertical_body{mine->position.y, 0.0f, true};
    state.cart.unstepped_seconds = 0.0f;
    state.cart.steer = 0.0f;
    if (mine->condition != cart_condition::upright && driving_cart(state)) {
        leave_cart_at_once(state);
    }
}

std::vector<cart_in_world> carts_in_world(const game_state& state) {
    std::vector<cart_in_world> carts;
    if (!course_started(state)) {
        return carts;
    }
    if (const auto mine = shown_cart(state, 0)) {
        cart_pose drawn = *mine;
        if (!driving_cart(state) && state.tuning.net.remote_correction_seconds > 0.0f) {
            const float blend = state.cart.exit_correction_left / state.tuning.net.remote_correction_seconds;
            drawn.position += state.cart.exit_correction * blend;
            drawn.yaw = wrap_angle(drawn.yaw + state.cart.exit_yaw_correction * blend);
        }
        carts.push_back(cart_in_world{0, drawn, cart_condition_of(state, 0), driving_cart(state), state.cart.steer,
                                      equipped_loadout(state).cart, my_cart_speed(state), fall_angle_of(state, 0)});
    }
    if (!is_online(state)) {
        return carts;
    }
    for (const auto& [owner, cart] : state.online.carts) {
        if (is_mine(state, owner)) {
            continue;
        }
        const auto driver = state.remote_avatars.find(owner);
        const bool driven = cart.driver != 0 && driver != state.remote_avatars.end() && driver->second.mode == motion_mode::cart &&
            cart.condition == cart_condition::upright;
        const std::string item = others_cart_item(state, owner);
        const auto player = state.online.players.find(owner);
        const float speed = driven && player != state.online.players.end() && player->second.motion ? player->second.motion->speed : 0.0f;
        if (const std::optional<cart_pose> pose = shown_cart(state, owner)) {
            carts.push_back(cart_in_world{owner, *pose, cart.condition, driven, driven ? remote_steer(state, owner, item) : 0.0f,
                                          item, speed, fall_angle_of(state, owner)});
        }
    }
    return carts;
}
