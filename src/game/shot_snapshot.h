#pragma once

// What moves that a shot meets, as it stood when the shot was hit: the
// people on foot, the carts and the balls at rest. Online the server takes
// it from its rows and writes it into the shot's row, so every client
// replays the same flight; offline it is me and my cart. Someone moving
// into the flight afterwards is not hit. Pure, shared with the server.

#include "game/game_tuning.h"
#include "physics/solid.h"

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

enum class snapshot_kind : std::uint8_t {
    person,
    cart,
    ball
};

struct snapshot_entry {
    snapshot_kind kind = snapshot_kind::person;
    // A person's feet, a cart's ground under the middle of its seat, a ball's centre.
    glm::vec3 position{0.0f};
    float yaw = 0.0f;  // a cart's
    bool shooter = false;  // the one who hit the shot
};

// The entries as solids that stop the ball: a person a cylinder from their
// feet up (player.body_radius, rangefinder.person_height), a cart its body's
// box, a ball its own size. The shooter's last, when there is one.
std::vector<solid> snapshot_solids(const std::vector<snapshot_entry>& entries, const game_tuning& tuning);
