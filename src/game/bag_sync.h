#pragma once

// Online, my bag and the club in my hand against my player_bag row, which
// is their truth. A bag move or club taken here (game/golf_bag.h) plays
// ahead of the row until the row shows it, the server refuses it, or
// net.max_unacked_seconds pass without either; then the whole row wins. A
// row change I did not ask for (my bag set down as the server tows or
// removes the cart it was strapped on, the putter a warmup station hands
// me) is taken at once, keeping only what I am still waiting for as I
// predicted it. A row that shows my prediction changes nothing here, so its
// handling plays on. Offline nothing here runs.

#include "game/bag_place.h"

#include <optional>
#include <string>

struct game_state;

// What I sent that my row has yet to show, each with how long it has waited.
struct bag_expectation {
    std::optional<bag_place> place;
    float place_seconds = 0.0f;
    std::optional<std::string> club;
    float club_seconds = 0.0f;
};

// A place_bag sent: the row should come to have my bag where it now is.
void expect_bag_move(game_state& state);
// A take_club sent: the row should come to have `club_id` in my hand.
void expect_club_taken(game_state& state, const std::string& club_id);

// My bag and club as my row has them, whole, waiting for nothing: arrival,
// a refused move or club. Nothing until the row exists.
void adopt_bag_row(game_state& state);

// Every frame: a changed row is taken in, around what I still wait for.
void reconcile_bag_row(game_state& state, float dt);
