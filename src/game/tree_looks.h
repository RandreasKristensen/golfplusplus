#pragma once

// Reads lighting.json's "trees": how each species is drawn (tree_look in
// game/lighting_loader.h). Every field must be present: a species' name
// once, a known crown shape, sizes in range and colours in 0..1.

#include "game/json_util.h"
#include "game/lighting_loader.h"

#include <optional>
#include <vector>

std::optional<std::vector<tree_look>> parse_tree_looks(const json& root);
