#pragma once

// Everything F acts on, in reach of me on foot: my ball, my ball in its cup, a
// collectible, a nearby online golfer, an unplayed hole's tee (every station's on a practice
// ground), my bag, my bag on a cart that has gone over, a tipped cart.
// Anything in reach can be used at any time; the nearest wins, and where two
// are as near, the kind listed first. Cart seats are walked into, not F'd
// (golf_cart.h walk_into_seat).

#include "game/game_state.h"
#include "game/golf_cart.h"

#include <cstddef>
#include <optional>
#include <vector>

enum class interactable_kind {
    dap,
    tee_book,
    bus,
    booked_start,
    warmup,
    practice_ball, // F picks it up; swing beside it addresses it
    ball,         // my ball, teed or at rest: take it up to play it
    cup_ball,     // my ball in its cup: pick it up
    collectible,  // by index into world.collectibles
    tee,          // an unplayed hole's tee, by hole index
    bag,          // my bag, set down or on a cart: its club picker
    fallen_bag,   // my bag on a cart on its side or sunk: onto my back
    tipped_cart   // a cart on its side: stand it back up
};

struct interactable {
    interactable_kind kind = interactable_kind::ball;
    std::size_t index = 0;  // collectible and tee
    float distance = 0.0f;  // from me (XZ)
    reachable_tipped_cart tipped;  // tipped_cart only
};

// Every interactable in reach, in no particular order.
std::vector<interactable> interactables_in_reach(const game_state& state);

// The one F acts on, or nullopt when nothing is in reach.
std::optional<interactable> nearest_interactable(const game_state& state);
