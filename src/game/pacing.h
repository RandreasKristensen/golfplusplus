#pragma once

// Pacing a distance off on foot, as golfers pace a putt or a chip: Caps
// Lock starts counting from where I stand, every pacing.pace_meters of
// level ground I walk is a pace, and the count pops up as each lands, as a
// shot's number does. Caps Lock again stops it; so do running, a hop, Esc,
// leaving my feet or the walk. Its last count then fades. Presentation and
// aiming only: it never changes a shot. Steps use pacing.walk_factor of my
// item-modified walking speed.

#include "game/game_input.h"

struct game_state;

// The count shown: paces so far, and how long since the last landed.
struct pace_count {
    int paces = 0;
    float age = 0.0f;
};

// Counting now: the level metres walked since it started.
struct pacing_walk {
    float meters = 0.0f;
};

// Whole paces in `meters` of `pace_meters` each.
int paces_in(float meters, float pace_meters);

// Applies the pacing toggle and cancellations before this frame moves.
void update_pacing(game_state& state, const game_input& input);
// Counts the actual level metres traveled by one fixed step after collision.
void count_paces(game_state& state, float meters);

// Ages the count shown: held while pacing, gone after its lifetime once
// stopped. Pacing stops off my feet or the walk.
void update_pace_count(game_state& state, float dt);
