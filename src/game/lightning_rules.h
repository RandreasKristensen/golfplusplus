#pragma once

// Lightning striking golfers: how likely a golfer is to be struck over one
// check of the shared clock, from the storm, what is over their head and
// whether a club is in their hands, and a seeded roll of who is struck and
// whether it knocks out or dazes. Pure and shared by the server (which rolls
// it for everyone in a room from its secret) and the offline game (from a
// session seed). Real lightning safety: only a building's roof shelters; a
// tree's canopy is worse than the open (side flash); a cart is no shelter.

#include "game/game_tuning.h"
#include "physics/solid.h"

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

// What is over a golfer's head.
enum class lightning_cover {
    open,      // nothing, or only what is neither shelter nor danger (a cart, a lamp)
    tree,      // a tree's canopy
    building   // a building's roof
};

enum class lightning_outcome {
    dazed,
    knocked_out
};

// A golfer as a check sees them.
struct lightning_golfer {
    std::uint64_t account = 0;  // 0: me, offline
    glm::vec3 position{0.0f};   // their feet, world units
    bool club_in_hand = false;  // at address or in the swing
};

struct lightning_strike_hit {
    std::uint64_t account = 0;
    glm::vec3 position{0.0f};
    lightning_outcome outcome = lightning_outcome::dazed;
};

// 0 at or below thunderstorm_thunder (game/weather.h), rising linearly to 1 at full thunder.
float storm_factor(float thunder);

// The nearest thing over `feet`'s head: a leaves solid (a tree's canopy) is a
// tree; a box of kind board (a hub building's body) with its underside over
// the head is a building's roof. Everything else is open. No shipped building
// can be stood under yet (its body box is solid to walkers), so the roof rule
// serves a building with a covered part.
lightning_cover cover_over(const std::vector<solid>& solids, const solid_grid& grid, const glm::vec3& feet,
                           const game_tuning& tuning);

// A golfer's chance of a strike per second: one per open_strike_hours at full
// thunder in the open, times the storm, the cover and the club in hand.
float strike_rate_per_second(float thunder, lightning_cover cover, bool club_in_hand, const lightning_tuning& tuning);

// The chance of at least one strike over `seconds` at `rate_per_second`.
float strike_chance(float rate_per_second, float seconds);

// The check of the shared clock `unix_ms` falls in: the same number for
// everyone in the same lightning.check_seconds.
std::int64_t lightning_check_at(std::int64_t unix_ms, const lightning_tuning& tuning);

// The dice of check number `check` of a world with `seed` (the server's
// secret, or an offline session's).
std::uint64_t lightning_check_seed(std::uint64_t seed, std::int64_t check);

// Who is struck over one check of lightning.check_seconds at `thunder`, and
// how: knocked out with probability lightning.knockout_share, else dazed. Each
// golfer rolls their own dice from `check_seed` and their account. Nobody at
// or below thunderstorm_thunder.
std::vector<lightning_strike_hit> lightning_strikes(const std::vector<lightning_golfer>& golfers, float thunder,
                                                    std::uint64_t check_seed, const std::vector<solid>& solids,
                                                    const solid_grid& grid, const game_tuning& tuning);
