#pragma once

// Me on foot and my cart bumping into the area's solids that stop us
// (play_area::solids) and into each other: on foot I stop at every cart, and
// my cart at people on foot and other carts. A walkable top (a tee box) low
// enough to step up onto does not stop me: a walker's step_up is a stair,
// a cart's a kerb. On foot I slide along what I meet; a cart scrapes, losing
// most of its way (cart.scrape_friction). The first touch on foot is heard;
// a cart crashing into something bounces back off it by its restitution,
// heard from cart.crash_sound_speed. Up and down, both follow the one
// surface (sample_area), fall off what is higher than a step and land.

struct game_state;

#include "physics/solid.h"
#include "profiling/profiling.h"

#include <vector>

#include <glm/vec3.hpp>

// On foot, walked here from `before` (my feet's height then): kept clear, a
// bump heard as I walk into something.
void keep_walker_clear(game_state& state, const glm::vec3& before);
// What moves that my cart bumps into besides the area's solids: other carts,
// people on foot and the flagsticks standing in their cups. Built once a frame.
std::vector<solid> cart_obstacles(const game_state& state);
// My cart, driven here from `before`: kept clear of the area's solids and
// `others`, bouncing off what it crashes into.
void keep_cart_clear(game_state& state, const glm::vec3& before, const std::vector<solid>& others);

// My cart's height over `dt`: on the surface under it, or falling onto it.
void settle_cart(game_state& state, float dt, frame_profile* profile);
