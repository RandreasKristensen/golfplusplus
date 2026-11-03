#pragma once

// Everything that stands on a play area's ground as one list of solids
// (physics/solid.h), built once with the area (play_area::solids): tree trunks
// and their leaves, the poles of signs, fences and floodlights and the
// floodlights' lamp heads, the signs' boards, the fences' nets and the tee
// boxes. The ball meets all of them, the leaves as a porous canopy it flies
// through; people and carts everything but leaves and lamp heads, a tee
// box's top being walkable (stepped onto when low enough,
// physics/body_collision.h). Restitution and friction come from the
// tuning's `solids`. The flagsticks are solids too, but of the flags'
// state, built when needed (flagstick_solids). Pure.

#include "game/game_tuning.h"
#include "game/hole_data.h"
#include "game/tee_box.h"
#include "physics/cup.h"
#include "physics/solid.h"

#include <vector>

struct play_area;

// The solids standing on `area` (its ground, tee boxes, signs, fences and
// floodlights finished).
std::vector<solid> standing_solids(const play_area& area, const game_tuning& tuning);

// The tee boxes alone, as standing_solids makes them.
std::vector<solid> tee_box_solids(const std::vector<tee_box>& boxes, const game_tuning& tuning);

// Every green's flagstick by its flag (`flags_in` by hole; a hole past its
// end has its flag in): in, standing in its cup, its real thickness to the
// ball and scale.flagstick_radius_meters to people and carts; out, lying on
// the green beside the cup, which only the ball meets.
std::vector<solid> flagstick_solids(const std::vector<cup>& cups, const std::vector<bool>& flags_in,
                                    const game_tuning& tuning);
