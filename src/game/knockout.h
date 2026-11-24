#pragma once

// Bodies in the world as the game reads them, and being knocked out offline.
// A knocked-out golfer lies where they fell (game/fallen_body.h): online the
// room's body rows (game_state::online), offline the save's bodies in this
// place. Offline only lightning knocks me out; it calls knock_out_offline.

#include "game/fallen_body.h"

#include <cstdint>
#include <vector>

struct game_state;

// The bodies lying where I am now: online my room's rows (the server deletes
// them as they expire), offline the save's in this place not yet expired by
// the machine's clock.
std::vector<fallen_body> bodies_in_world(const game_state& state);

// Knocks me out where I stand at `now_ms` (UTC): my body is added to the
// save, which is asked to be written, and `state.knocked_out` tells app to
// return to the main menu. Offline only; online the server does this.
void knock_out_offline(game_state& state, std::int64_t now_ms);
