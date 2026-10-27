#pragma once

// Everything F acts on, in reach of me on foot: my ball, my ball in its cup, a
// collectible, an unplayed hole's tee, my bag, a cart seat, a tipped cart. Anything in reach can be
// used at any time; the nearest wins, and where two are as near, the kind
// listed first.

#include "game/game_state.h"
#include "game/golf_cart.h"

#include <cstddef>
#include <optional>
#include <vector>

enum class interactable_kind {
    ball,         // my ball, teed or at rest: take it up to play it
    cup_ball,     // my ball in its cup: pick it up
    collectible,  // by index into world.collectibles
    tee,          // an unplayed hole's tee, by hole index
    bag,          // my bag, set down or on my cart: its club picker
    seat,         // a cart seat
    tipped_cart   // a cart on its side: stand it back up
};

struct interactable {
    interactable_kind kind = interactable_kind::ball;
    std::size_t index = 0;  // collectible and tee
    float distance = 0.0f;  // from me (XZ)
    reachable_seat seat;    // seat only
    reachable_tipped_cart tipped;  // tipped_cart only
};

// Every interactable in reach, in no particular order.
std::vector<interactable> interactables_in_reach(const game_state& state);

// The one F acts on, or nullopt when nothing is in reach.
std::optional<interactable> nearest_interactable(const game_state& state);
