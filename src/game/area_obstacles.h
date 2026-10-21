#pragma once

// What stands on a play area's ground, as players and carts bump into it
// (physics/body_collision.h): tree trunks, the poles of floodlights,
// fences and signs, the signs' boards, the fences' nets and every hole's
// flagstick. Carts are also stopped by the tee boxes, as solid blocks.
// Built with the area (play_area::obstacles). Pure.

#include "game/game_tuning.h"
#include "game/hole_data.h"
#include "physics/body_collision.h"

#include <vector>

struct play_area;

// The obstacles of `area`, standing; `holes` placed in its coordinates.
upright_obstacles standing_obstacles(const play_area& area, const std::vector<hole_data>& holes,
                                     const game_tuning& tuning);
// The area's tee boxes, which stop carts only.
upright_obstacles cart_only_obstacles(const play_area& area);
