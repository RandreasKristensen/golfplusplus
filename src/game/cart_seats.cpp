#include "game/cart_seats.h"

#include "physics/vector_math.h"

#include <cmath>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

namespace {
// Towards `seat`'s side of the cart.
glm::vec3 seat_side(const cart_pose& cart, const cart_seat seat) {
    const glm::vec3 left = yaw_left(yaw_direction(cart.yaw));
    return seat == cart_seat::driver ? left : -left;
}
}

float game_top_speed(const cart_spec& spec, const cart_tuning& tuning) {
    return spec.top_speed_mps * tuning.time_scale;
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

glm::vec3 bag_spot(const cart_pose& cart, const cart_seat seat, const cart_tuning& tuning) {
    return seat_position(cart, seat, tuning) - yaw_direction(cart.yaw) * tuning.bag_behind_seat;
}

solid cart_solid(const cart_pose& pose, const cart_tuning& tuning, const float height) {
    const glm::vec3 forward = yaw_direction(pose.yaw);
    solid cart;
    cart.shape = solid_shape::box;
    cart.kind = solid_kind::cart;
    cart.base = pose.position + forward * (0.5f * (tuning.body_front - tuning.body_back));
    cart.along = forward;
    cart.half_length = 0.5f * (tuning.body_front + tuning.body_back);
    cart.half_width = tuning.body_half_width;
    cart.height = height;
    cart.stops_walker = true;
    cart.stops_cart = true;
    return cart;
}

glm::vec3 seat_side_direction(const cart_pose& cart, const cart_seat seat) {
    return seat_side(cart, seat);
}

bool at_seat_door(const cart_pose& cart, const cart_seat seat, const glm::vec3& position, const cart_tuning& tuning) {
    const glm::vec3 offset = position - cart.position;
    const float out = glm::dot(glm::vec2(offset.x, offset.z), glm::vec2(seat_side(cart, seat).x, seat_side(cart, seat).z));
    const glm::vec3 forward = yaw_direction(cart.yaw);
    const float along = offset.x * forward.x + offset.z * forward.z;
    return out >= tuning.body_half_width && out <= tuning.body_half_width + tuning.door_depth &&
        std::abs(along) <= tuning.door_half_length;
}

cart_seat other_seat(const cart_seat seat) {
    return seat == cart_seat::driver ? cart_seat::passenger : cart_seat::driver;
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
