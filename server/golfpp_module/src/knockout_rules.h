#pragma once

// What a knockout does, as plain data for the reducers and the native tests:
// whom a knockout still lands on, and the body it leaves where the ball met
// them (game/fallen_body.h). The rows are written in lib.cpp.

#include "game/fallen_body.h"

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/vec3.hpp>

// Whether a knockout meant for room `room_id` lands: the golfer is still in
// that room (`member_room`, nullopt when in none) and the server knows where
// they stand.
bool knockout_lands(std::optional<std::uint64_t> member_room, std::uint64_t room_id, bool has_position);

// What the reducer reads of a golfer about to be knocked out.
struct knocked_golfer {
    std::uint64_t account = 0;
    glm::vec3 position{0.0f};  // the server's accepted position
    float yaw = 0.0f;
    bool bag_on_back = false;
    std::uint8_t colour = 0;
    handedness hand = handedness::right;
};

// The body they leave, fallen at `at_ms`.
fallen_body body_after_knockout(const knocked_golfer& golfer, std::int64_t at_ms, const knockout_tuning& tuning);
