#pragma once

// Where a golfer's bag is: strapped behind a seat of a cart (theirs, or
// another's with that spot free: a cart carries two bags), on their back,
// or set down on the ground (game/golf_bag.h plays it; the server
// checks moves and clubs taken from it). Plain data, shared with the server.

#include "game/cart_seats.h"

#include <cstdint>

#include <glm/vec3.hpp>

enum class bag_where : std::uint8_t {
    on_cart,
    on_back,
    on_ground
};

struct bag_place {
    bag_where where = bag_where::on_back;
    cart_seat seat = cart_seat::driver;  // on_cart: behind which seat
    std::uint64_t cart_owner = 0;        // on_cart: whose cart, 0 the golfer's own
    glm::vec3 position{0.0f};            // on_ground
    float yaw = 0.0f;                    // on_ground: the way it faces
};

// Whether `a` and `b` are the same place.
bool same_place(const bag_place& a, const bag_place& b);

// Where `bag` is now: behind its seat of `cart` (the one it is strapped
// on), on the golfer standing at `golfer`, or where it was set down.
glm::vec3 bag_at(const bag_place& bag, const cart_pose& cart, const glm::vec3& golfer, const cart_tuning& tuning);
