#pragma once

// My bag and the one club in my hand. The bag is strapped behind a seat of
// a cart (mine, or online anyone's with that spot free, so a cart carries
// two bags and goes where its driver takes it), on my back or set down on
// the ground, and stays where it is from hole to hole. Clubs change only at
// the bag, in its picker (F at it); B moves it, F takes it off a cart that
// has gone over. Each move and club change is handled for a moment
// (game/bag_handling.h). I cannot step up to my ball with it on my back.

#include "game/game_input.h"
#include "game/game_state.h"

#include <optional>

#include <glm/vec3.hpp>

// Where my bag is in the world: behind its seat on the cart it is strapped
// on, on my back (where I stand), or where it was set down.
glm::vec3 bag_position(const game_state& state);
// The owner of the cart my bag is strapped on (0: mine).
std::uint64_t bag_cart_owner(const game_state& state);

bool bag_on_back(const game_state& state);

// How far (XZ) my bag is from me on foot, when it is in reach to take a club
// from (player.bag_reach): set down, or strapped on a cart while it stands
// on its wheels.
std::optional<float> bag_in_reach(const game_state& state);
// How far my bag is, when it is in reach strapped on a cart that has gone
// over (on its side or sunk): F takes it onto my back.
std::optional<float> bag_on_fallen_cart_in_reach(const game_state& state);

// A round starts with my bag strapped on my cart behind its driver's seat,
// or on my back where I have no cart.
void strap_bag_on_cart(game_state& state);

// F at my bag: the picker opens on the club in hand.
void open_club_picker(game_state& state);

// The picker, while it is open: up the list (a longer club) and down it,
// F or a left click takes the highlighted club (putting back the one in
// hand), Escape closes it. True while it was open this frame: the input
// was its.
bool update_club_picker(game_state& state, const game_input& input);

// B on foot: off a cart or the ground onto my back when it is in reach;
// from my back onto the nearest cart spot in reach (refused with a notice
// when another's bag is there), else down at my feet.
void move_bag(game_state& state);
// Onto my back from wherever it is, when it is in reach.
void take_bag_on_back(game_state& state);
