#pragma once

// Cart decisions shared by reducers and the native sync harness. Inputs are
// complete row values; a successful result replaces the cart row atomically.
// Driving, tipping and sinking are the server's own steps (movement_rules.h).

#include "game/net_types.h"
#include "game/play_area.h"
#include "play_rules.h"

#include <cstdint>
#include <optional>
#include <string>

struct cart_decision {
    std::optional<room_cart> cart;
    std::string error;
};

cart_decision take_wheel_rule(const room_cart& cart, std::uint64_t account, const glm::vec3& player,
                              const game_tuning& tuning);
cart_decision leave_wheel_rule(const room_cart& cart, std::uint64_t account);
cart_decision right_cart_rule(const room_cart& cart, const glm::vec3& player, const game_tuning& tuning);
cart_decision tow_cart_rule(const room_cart& cart, const cart_pose& parking);
