#pragma once

// What moves that a shot meets, as it stood when the shot was hit: the
// people on foot, the carts and the balls at rest. Online the server takes
// it from its rows and writes it into the shot's row, so every client
// replays the same flight; offline it is me and my cart. Someone moving
// into the flight afterwards is not hit. Pure, shared with the server.

#include "game/game_tuning.h"
#include "physics/solid.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/vec3.hpp>

enum class snapshot_kind : std::uint8_t {
    person,
    cart,
    ball,
    body  // a knocked-out golfer lying (game/fallen_body.h)
};

struct snapshot_entry {
    snapshot_kind kind = snapshot_kind::person;
    // A person's feet, a cart's ground under the middle of its seat, a ball's
    // centre, the middle of a body.
    glm::vec3 position{0.0f};
    float yaw = 0.0f;  // a cart's or a body's
    bool shooter = false;  // the one who hit the shot
    // A person's account online; 0 for anyone else and for offline's me.
    std::uint64_t account = 0;
};

// The entries as solids that stop the ball: a person a cylinder from their
// feet up (player.body_radius, rangefinder.person_height), a cart its body's
// box, a body its low box (body_solid), a ball its own size. The shooter's last, when there is one.
std::vector<solid> snapshot_solids(const std::vector<snapshot_entry>& entries, const game_tuning& tuning);

// One entry as the solid that stops the ball (what snapshot_solids makes of it).
solid snapshot_solid(const snapshot_entry& entry, const game_tuning& tuning);

// The entries that never move during a shot (balls at rest, bodies lying) and
// those that do (people on foot and carts, with their accounts), each in the
// order given. The server plays a flight against the first and finds the
// second as their movement is played (ball_contact.h).
struct snapshot_split {
    std::vector<snapshot_entry> still;
    std::vector<snapshot_entry> movers;
};
snapshot_split split_snapshot(const std::vector<snapshot_entry>& entries);

// The entry of `entries` that solid number `solid_index` of snapshot_solids
// stands for (the shooter's solid being last), or none past the end.
std::optional<std::size_t> snapshot_entry_of_solid(const std::vector<snapshot_entry>& entries, std::size_t solid_index);
