#include "game/golf_bag.h"

#include "game/bag_handling.h"
#include "game/golf_cart.h"
#include "game/mode_dispatch.h"
#include "game/text_ids.h"
#include "game/world_sounds.h"
#include "physics/vector_math.h"

#include <algorithm>

namespace {
bool is_mine(const game_state& state, const std::uint64_t owner) {
    return owner == 0 || owner == state.online.account_id;
}

// The owner of the cart `bag` (of `account`'s) is strapped on, as a cart
// owner (0: mine).
std::uint64_t cart_of(const game_state& state, const bag_place& bag, const std::uint64_t account) {
    const std::uint64_t owner = bag.cart_owner != 0 ? bag.cart_owner : account;
    return is_mine(state, owner) ? 0 : owner;
}

// Whether a bag other than mine is strapped behind `seat` of `owner`'s cart.
bool spot_taken(const game_state& state, const std::uint64_t owner, const cart_seat seat) {
    for (const auto& [account, bag] : state.online.bags) {
        if (!is_mine(state, account) && bag.place.where == bag_where::on_cart && bag.place.seat == seat &&
            cart_of(state, bag.place, account) == owner) {
            return true;
        }
    }
    return false;
}

// A spot behind a seat of a cart standing on its wheels, free or not.
struct cart_spot {
    std::uint64_t owner = 0;  // 0: mine
    cart_seat seat = cart_seat::driver;
    bool taken = false;
};

// The nearest cart spot within reach of me: on my cart, and online on
// anyone's.
std::optional<cart_spot> cart_spot_in_reach(const game_state& state) {
    std::optional<cart_spot> nearest;
    float best = state.tuning.player.bag_reach;
    const auto consider = [&](const std::uint64_t owner) {
        const std::optional<cart_pose> cart = shown_cart(state, owner);
        if (!cart || cart_condition_of(state, owner) != cart_condition::upright) {
            return;
        }
        for (const cart_seat seat : {cart_seat::driver, cart_seat::passenger}) {
            const float distance = horizontal_distance(state.player.position, bag_spot(*cart, seat, state.tuning.cart));
            if (distance <= best) {
                best = distance;
                nearest = cart_spot{owner, seat, spot_taken(state, owner, seat)};
            }
        }
    };
    if (own_cart(state)) {
        consider(0);
    }
    if (is_online(state)) {
        for (const auto& [owner, cart] : state.online.carts) {
            if (!is_mine(state, owner)) {
                consider(owner);
            }
        }
    }
    return nearest;
}

void place_bag(game_state& state, const bag_place& place) {
    const bag_place from = state.bag;
    state.bag = place;
    record_bag_moved(state);
    push_audio_event(state, audio_event_type::bag_moved, bag_position(state));
    start_bag_handling(state, from);
}

// The cart my bag is strapped on stands on its wheels.
bool bag_cart_upright(const game_state& state) {
    return state.bag.where != bag_where::on_cart || cart_condition_of(state, bag_cart_owner(state)) == cart_condition::upright;
}
}

std::uint64_t bag_cart_owner(const game_state& state) {
    return cart_of(state, state.bag, 0);
}

glm::vec3 bag_position(const game_state& state) {
    const std::optional<cart_pose> cart = state.bag.where == bag_where::on_cart ? shown_cart(state, bag_cart_owner(state))
                                                                                : std::nullopt;
    if (state.bag.where == bag_where::on_cart && !cart) {
        return state.player.position;
    }
    return bag_at(state.bag, cart.value_or(cart_pose{}), state.player.position, state.tuning.cart);
}

bool bag_on_back(const game_state& state) {
    return state.bag.where == bag_where::on_back;
}

std::optional<float> bag_in_reach(const game_state& state) {
    if (state.ride || bag_on_back(state) || !bag_cart_upright(state)) {
        return std::nullopt;
    }
    const float distance = horizontal_distance(state.player.position, bag_position(state));
    return distance <= state.tuning.player.bag_reach ? std::optional<float>(distance) : std::nullopt;
}

std::optional<float> bag_on_fallen_cart_in_reach(const game_state& state) {
    if (state.ride || state.bag.where != bag_where::on_cart || bag_cart_upright(state)) {
        return std::nullopt;
    }
    const float distance = horizontal_distance(state.player.position, bag_position(state));
    return distance <= state.tuning.player.bag_reach ? std::optional<float>(distance) : std::nullopt;
}

void strap_bag_on_cart(game_state& state) {
    state.bag = own_cart(state) ? bag_place{bag_where::on_cart, cart_seat::driver, 0, glm::vec3(0.0f), 0.0f} : bag_place{};
    state.handling.reset();
}

void open_club_picker(game_state& state) {
    state.picker = club_picker{state.selected_club};
}

bool update_club_picker(game_state& state, const game_input& input) {
    if (!state.picker) {
        return false;
    }
    if (input.cancel || !bag_in_reach(state) || state.clubs.empty()) {
        state.picker.reset();
        return true;
    }
    // Longest first: up the list is a longer club.
    const int last = static_cast<int>(state.clubs.size()) - 1;
    state.picker->highlighted = static_cast<std::size_t>(
        std::clamp(static_cast<int>(state.picker->highlighted) - input.picker_steps, 0, last));
    if (input.interact || input.take_club) {
        if (state.selected_club != state.picker->highlighted) {
            const std::string put_back = state.clubs[state.selected_club].id;
            state.selected_club = state.picker->highlighted;
            record_club_taken(state, state.clubs[state.selected_club].id);
            push_audio_event(state, audio_event_type::club_change, bag_position(state));
            start_club_swap(state, put_back);
        }
        state.picker.reset();
    }
    return true;
}

void take_bag_on_back(game_state& state) {
    if (state.ride || bag_on_back(state) ||
        horizontal_distance(state.player.position, bag_position(state)) > state.tuning.player.bag_reach) {
        return;
    }
    place_bag(state, bag_place{bag_where::on_back, cart_seat::driver, 0, glm::vec3(0.0f), 0.0f});
}

void move_bag(game_state& state) {
    if (state.ride) {
        return;
    }
    if (!bag_on_back(state)) {
        take_bag_on_back(state);
        return;
    }
    if (const std::optional<cart_spot> spot = cart_spot_in_reach(state)) {
        if (spot->taken) {
            state.notice = game_notice{text_hud_bag_spot_taken, 0.0f};
            return;
        }
        place_bag(state, bag_place{bag_where::on_cart, spot->seat, spot->owner, glm::vec3(0.0f), 0.0f});
        return;
    }
    // At my feet, to my right.
    const glm::vec3 right = -yaw_left(yaw_direction(state.player.yaw));
    glm::vec3 at = state.player.position + right * state.tuning.player.bag_set_down_distance;
    at.y = terrain_height(state.area, at);
    place_bag(state, bag_place{bag_where::on_ground, cart_seat::driver, 0, at, state.player.yaw});
}
