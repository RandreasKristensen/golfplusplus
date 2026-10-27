#include "game/golf_bag.h"

#include "game/mode_dispatch.h"
#include "game/world_sounds.h"
#include "physics/vector_math.h"

#include <algorithm>

namespace {
// The seat of my cart whose bag spot is nearest me, within reach.
std::optional<cart_seat> cart_spot_in_reach(const game_state& state) {
    std::optional<cart_seat> nearest;
    float best = state.tuning.player.bag_reach;
    for (const cart_seat seat : {cart_seat::driver, cart_seat::passenger}) {
        const float distance = horizontal_distance(state.player.position, bag_spot(state.cart.pose, seat, state.tuning.cart));
        if (distance <= best) {
            best = distance;
            nearest = seat;
        }
    }
    return nearest;
}

void place_bag(game_state& state, const bag_place& place) {
    state.bag = place;
    record_bag_moved(state);
    push_audio_event(state, audio_event_type::bag_moved, bag_position(state));
}
}

glm::vec3 bag_position(const game_state& state) {
    return bag_at(state.bag, state.cart.pose, state.player.position, state.tuning.cart);
}

bool bag_on_back(const game_state& state) {
    return state.bag.where == bag_where::on_back;
}

std::optional<float> bag_in_reach(const game_state& state) {
    // Not from a cart on its side or under water.
    const bool cart_down = state.bag.where == bag_where::on_cart && state.cart.motion.condition != cart_condition::upright;
    if (state.ride || bag_on_back(state) || cart_down) {
        return std::nullopt;
    }
    const float distance = horizontal_distance(state.player.position, bag_position(state));
    return distance <= state.tuning.player.bag_reach ? std::optional<float>(distance) : std::nullopt;
}

void strap_bag_on_cart(game_state& state) {
    state.bag = bag_place{bag_where::on_cart, cart_seat::driver, glm::vec3(0.0f), 0.0f};
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
            state.selected_club = state.picker->highlighted;
            record_club_taken(state, state.clubs[state.selected_club].id);
            push_audio_event(state, audio_event_type::club_change, bag_position(state));
        }
        state.picker.reset();
    }
    return true;
}

void move_bag(game_state& state) {
    if (state.ride) {
        return;
    }
    switch (state.bag.where) {
    case bag_where::on_back:
        if (const std::optional<cart_seat> seat = cart_spot_in_reach(state)) {
            place_bag(state, bag_place{bag_where::on_cart, *seat, glm::vec3(0.0f), 0.0f});
        } else {
            // At my feet, to my right.
            const glm::vec3 right = -yaw_left(yaw_direction(state.player.yaw));
            glm::vec3 at = state.player.position + right * state.tuning.player.bag_set_down_distance;
            at.y = terrain_height(state.area, at);
            place_bag(state, bag_place{bag_where::on_ground, cart_seat::driver, at, state.player.yaw});
        }
        return;
    case bag_where::on_cart:
    case bag_where::on_ground:
        if (horizontal_distance(state.player.position, bag_position(state)) <= state.tuning.player.bag_reach) {
            place_bag(state, bag_place{bag_where::on_back, cart_seat::driver, glm::vec3(0.0f), 0.0f});
        }
        return;
    }
}
