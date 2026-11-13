#pragma once

// The number a shot pops up as it is struck: how far it went as struck,
// with its dice, but in neutral conditions (sea level at the standard
// temperature, still dry air, a level stance and level ground): its carry,
// a putt's roll on a level green. So a 260 drive may land 180 up a hill into
// the wind in the rain. A crit when a full stock swing comes off within
// tuning.shot_numbers.crit_share of its carry of where it would land as set
// up. Pure.

#include "game/club_definition.h"
#include "game/game_tuning.h"
#include "game/golfer.h"
#include "game/shot_dice.h"

#include <optional>
#include <vector>

struct shot_number {
    float meters = 0.0f;
    bool crit = false;
};

// `record` struck by `golfer` from `lie`, a putt on greens of
// `green_stimp_feet`; none for an unknown club.
// How big a popped-up number is at `age` seconds, as a share of its box:
// start_size, shrinking evenly to end_size over its lifetime.
float shot_number_size(const shot_number_tuning& tuning, float age);

std::optional<shot_number> shot_number_for(const shot_record& record, const golfer_profile& golfer, ball_lie lie,
                                           float green_stimp_feet, const game_tuning& tuning,
                                           const std::vector<club_definition>& clubs);
