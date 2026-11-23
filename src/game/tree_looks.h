#pragma once

// Reads lighting.json's "trees": how each species is drawn (tree_look in
// game/lighting_loader.h) and its image grid. Every field must be present: a species' name
// once, a known crown shape, sizes in range and colours in 0..1.

#include "game/json_util.h"
#include "game/lighting_loader.h"

#include <optional>
#include <vector>

std::optional<std::vector<tree_look>> parse_tree_looks(const json& root);

// lighting.json's "trees"."impostor": 1 to 16 azimuths, one to eight rising
// elevations within -80..80, a tile of 8 to 256 pixels, errors of at least 0.
std::optional<tree_impostor_style> parse_tree_impostor_style(const json& root);

// lighting.json's "trees"."falling_leaves": every number present and in range.
std::optional<falling_leaf_style> parse_falling_leaf_style(const json& root);
