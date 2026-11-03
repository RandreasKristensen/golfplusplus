#pragma once

// Whose colour a bag and a cart are painted in, so each player knows theirs
// among the room's: the owner's favourite colour (the favourite_colour
// setting), the bag a darker shade and the cart a faded one of it
// (lighting.json's figures, bag_paints and cart_paints); with none picked
// (0), one picked by their account, so carts parked together differ.

#include <cstddef>
#include <cstdint>

struct game_state;

// A golfer's paint: their favourite colour (1 the first paint, 0 none
// picked) and their account.
struct gear_paint {
    std::uint8_t favourite = 0;
    std::uint64_t account = 0;
};

// Of `paint_count` paints, the one `paint` is.
std::size_t gear_paint_index(const gear_paint& paint, std::size_t paint_count);

// The paint of `owner`'s gear (0 or my account: mine).
gear_paint gear_paint_of(const game_state& state, std::uint64_t owner);
