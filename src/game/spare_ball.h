#pragma once

// One of my balls that is not the ball in play: it lies at rest where I put it
// (game/my_balls.h). Offline game_state::spare_balls; online a practice_ball row.
#include <cstdint>

#include <glm/vec3.hpp>

struct spare_ball {
    std::uint64_t id = 0;       // offline a running number, online the row's id; 0 only before a ball is kept
    std::uint64_t account = 0;  // online, whose it is; 0 offline (always mine)
    glm::vec3 position{0.0f};
    bool in_cup = false;
    // When it was first put on the ground (offline a running number, online
    // server microseconds): the oldest goes first when too many are out.
    std::int64_t placed_at = 0;
};
