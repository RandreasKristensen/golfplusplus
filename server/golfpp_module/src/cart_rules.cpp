#include "cart_rules.h"

#include "game/server_errors.h"

namespace {
cart_decision accepted(const room_cart& cart) { return {cart, {}}; }
cart_decision refused(const char* error) { return {std::nullopt, error}; }
}

cart_decision take_wheel_rule(const room_cart& cart, const std::uint64_t account, const glm::vec3& player,
                              const game_tuning& tuning) {
    if (cart.driver != 0) { return refused(error_seat_taken); }
    if (cart.condition != cart_condition::upright) { return refused(error_cart_overturned); }
    if (!within_seat_reach(cart_pose{cart.position, cart.yaw}, cart_seat::driver, player, tuning)) {
        return refused(error_too_far);
    }
    room_cart seated = cart;
    seated.driver = account;
    return accepted(seated);
}

cart_decision leave_wheel_rule(const room_cart& cart, const std::uint64_t account) {
    if (cart.driver != account || account == 0) { return refused(error_not_in_cart); }
    room_cart parked = cart;
    parked.driver = 0;
    return accepted(parked);
}

cart_decision right_cart_rule(const room_cart& cart, const glm::vec3& player, const game_tuning& tuning) {
    if (!cart_tipped(cart.condition)) { return refused(error_cart_upright); }
    if (!within_interact_reach(player, cart.position, tuning.cart.board_reach, tuning.server)) {
        return refused(error_too_far);
    }
    room_cart upright = cart;
    upright.condition = cart_condition::upright;
    return accepted(upright);
}

cart_decision tow_cart_rule(const room_cart& cart, const cart_pose& parking) {
    if (cart.condition != cart_condition::sunk) { return accepted(cart); }
    room_cart towed = cart;
    towed.position = parking.position;
    towed.yaw = parking.yaw;
    towed.condition = cart_condition::upright;
    towed.driver = 0;
    towed.passenger = 0;
    return accepted(towed);
}
