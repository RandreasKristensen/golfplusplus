#pragma once

// My bag and the one club in my hand. The bag is strapped on my cart behind
// a seat, on my back or set down on the ground, and stays where it is from
// hole to hole. Clubs change only at the bag, in its picker (F at it); B
// moves it. I cannot step up to my ball with it on my back.

#include "game/game_input.h"
#include "game/game_state.h"

#include <optional>

#include <glm/vec3.hpp>

// Where my bag is in the world: behind its seat on my cart, on my back
// (where I stand), or where it was set down.
glm::vec3 bag_position(const game_state& state);

bool bag_on_back(const game_state& state);

// How far (XZ) my bag is from me on foot, when it is in reach to take a club
// from (player.bag_reach): set down, or strapped on my cart while it stands
// on its wheels.
std::optional<float> bag_in_reach(const game_state& state);

// A round starts with my bag strapped on my cart behind its driver's seat.
void strap_bag_on_cart(game_state& state);

// F at my bag: the picker opens on the club in hand.
void open_club_picker(game_state& state);

// The picker, while it is open: up the list (a longer club) and down it,
// F or a left click takes the highlighted club (putting back the one in
// hand), Escape closes it. True while it was open this frame: the input
// was its.
bool update_club_picker(game_state& state, const game_input& input);

// B on foot: off my cart onto my back when its spot is in reach, onto my
// cart from my back when a spot is, else down at my feet; from the ground
// back onto my back when it is in reach.
void move_bag(game_state& state);
