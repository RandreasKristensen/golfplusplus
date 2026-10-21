#pragma once

// Me on foot and my cart bumping into what stands on the ground
// (play_area::obstacles; carts also into tee boxes) and into each other:
// on foot I stop at every cart, and my cart at people on foot and other
// carts. On foot I slide along what I meet; a cart scrapes, losing most of
// its way (cart.scrape_friction). The first touch is heard, and a cart
// crashing bounces back.

struct game_state;

#include <glm/vec3.hpp>

// On foot, walked here from `before`: kept clear, a bump heard as I walk
// into something.
void keep_walker_clear(game_state& state, const glm::vec3& before);
// My cart, driven here from `before`: kept clear; crashing into something
// it bounces back, heard from cart.crash_sound_speed, and pressed against
// it its speed is what it still moves.
void keep_cart_clear(game_state& state, const glm::vec3& before);
