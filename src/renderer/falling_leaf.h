#pragma once

// GL-free: where a tree's falling leaves are (unit tested, and the rendered
// check replays falling_leaf.vert against it). Presentation only: the game
// never reads it and nothing is kept per leaf. A leaf's place is a pure
// function of its tree's instance, its index and the clock: each cycle it
// leaves a random point of the crown's surface, falls at the style's speed
// with side-to-side flutter and a tumble, carried by the wind, lies a moment
// on the ground at the tree's foot height, shrinks away and starts again.
// How many of a tree's slots are in use is how many leaves its crown loses
// in a cycle (falling_leaves_per_cycle): a fractional slot is used in that
// share of cycles.

#include "game/lighting_loader.h"
#include "renderer/render_tree.h"
#include "renderer/tree_skeleton.h"

#include <cstdint>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

// One cycle of a tree's leaf: falling from its crown's top, then lying.
float falling_leaf_cycle_seconds(const falling_leaf_style& style, const render_tree_instance& tree);

// How many leaves fall from a tree in one cycle, so how many of its slots are
// in use, when `daily_share` of its full crown falls a day (game/tree_season.h's
// leaves_falling) in `wind_speed` m/s.
float falling_leaves_per_cycle(const falling_leaf_style& style, const render_tree_instance& tree, float daily_share,
                            float wind_speed);

// Whether slot `index` of `tree` drops a leaf in the cycle it is in at `seconds`.
bool falling_leaf_slot_used(const falling_leaf_style& style, const render_tree_instance& tree, int index, float per_cycle,
                            float seconds);

// A leaf as drawn: a flat quad round `centre`, `across` and `along` its half
// sides (falling_leaf.vert's corners).
struct falling_leaf {
    bool shown = false;
    bool lying = false;
    glm::vec3 centre{0.0f};
    glm::vec3 across{0.0f};
    glm::vec3 along{0.0f};
};

// Leaf `index` of `tree`, whose crown is its look's tree_crown_box and drops
// `per_cycle` leaves a cycle, at `seconds` in `wind` (m/s, x and z).
falling_leaf place_falling_leaf(const falling_leaf_style& style, const tree_box& crown, const render_tree_instance& tree,
                                int index, float per_cycle, float seconds, const glm::vec2& wind);

// The integer hash both sides use, so a leaf is the same leaf on the CPU and GPU.
std::uint32_t falling_leaf_hash(std::uint32_t x);
