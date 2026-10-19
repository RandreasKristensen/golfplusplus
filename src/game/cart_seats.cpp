#include "game/cart_seats.h"

#include "physics/vector_math.h"

namespace {
// Towards `seat`'s side of the cart.
glm::vec3 seat_side(const cart_pose& cart, const cart_seat seat) {
    const glm::vec3 left = yaw_left(yaw_direction(cart.yaw));
    return seat == cart_seat::driver ? left : -left;
}
}

glm::vec3 seat_position(const cart_pose& cart, const cart_seat seat, const cart_tuning& tuning) {
    return cart.position + seat_side(cart, seat) * tuning.seat_offset;
}

glm::vec3 step_out_position(const cart_pose& cart, const cart_seat seat, const cart_tuning& tuning) {
    return seat_position(cart, seat, tuning) + seat_side(cart, seat) * tuning.step_out_distance;
}

cart_pose cart_around_seat(const glm::vec3& feet, const float yaw, const cart_seat seat, const cart_tuning& tuning) {
    const cart_pose facing{feet, yaw};
    return cart_pose{feet - seat_side(facing, seat) * tuning.seat_offset, yaw};
}

bool in_seat_reach(const cart_pose& cart, const cart_seat seat, const glm::vec3& position, const cart_tuning& tuning) {
    return horizontal_distance(seat_position(cart, seat, tuning), position) <= tuning.board_reach;
}

cart_pose parked_cart_pose(const glm::vec3& start, const glm::vec3& pin, const std::size_t slot, const cart_tuning& tuning) {
    const float yaw = yaw_towards(start, pin);
    const glm::vec3 forward = yaw_direction(yaw);
    const glm::vec3 right = -yaw_left(forward);
    const float side = tuning.park_side_distance + static_cast<float>(slot) * tuning.park_spacing;
    return cart_pose{start + forward * tuning.park_ahead_distance + right * side, yaw};
}
