#pragma once

// Me on foot and my cart bumping into the area's solids that stop us
// (play_area::solids) and into each other, as the shared fixed steps do it
// (game/movement_step.h): on foot I stop at every cart, and my cart at
// people on foot and other carts, as they are shown here.

struct game_state;

#include "physics/solid.h"

#include <vector>

#include <glm/vec3.hpp>

// What stops me on foot besides the area's solids: every cart as shown and
// the flagsticks standing in their cups. Built once a step's frame.
std::vector<solid> walking_obstacles(const game_state& state);
// On foot, put here from `before`: kept clear (a correction from the server).
void keep_walker_clear(game_state& state, const glm::vec3& before);
// What moves that my cart bumps into besides the area's solids: other carts,
// people on foot and the flagsticks standing in their cups. Built once a frame.
std::vector<solid> cart_obstacles(const game_state& state);
