#pragma once

// Where a golf cart stands and where its seats are: plain geometry the game
// and the server share (game/golf_cart.h plays the cart; the server checks
// the reach to a seat and parks a room's carts with it). Pure.

#include "game/game_tuning.h"

#include <cstddef>

#include <glm/vec3.hpp>

// The driver's seat is on the cart's left, the passenger's on its right.
enum class cart_seat {
    driver,
    passenger
};

struct cart_pose {
    glm::vec3 position{0.0f};  // the ground under the middle of its seat
    float yaw = 0.0f;          // see yaw_direction in physics/vector_math.h
};

// Where someone sat in `seat` has their feet.
glm::vec3 seat_position(const cart_pose& cart, cart_seat seat, const cart_tuning& tuning);
// Where someone stepping out of `seat` stands: beside it, outside the cart.
glm::vec3 step_out_position(const cart_pose& cart, cart_seat seat, const cart_tuning& tuning);
// The cart someone sat in `seat` at `feet`, facing `yaw`, sits in.
cart_pose cart_around_seat(const glm::vec3& feet, float yaw, cart_seat seat, const cart_tuning& tuning);
// Close enough (XZ) to `seat` to step into it.
bool in_seat_reach(const cart_pose& cart, cart_seat seat, const glm::vec3& position, const cart_tuning& tuning);

// Parking spot `slot` beside the hole whose start is `start`, facing down it
// towards `pin`: park_ahead_distance down the hole and park_side_distance to
// its right, each further slot park_spacing further out. Its height is the
// start's; place it on the ground with the area's heights.
cart_pose parked_cart_pose(const glm::vec3& start, const glm::vec3& pin, std::size_t slot, const cart_tuning& tuning);
