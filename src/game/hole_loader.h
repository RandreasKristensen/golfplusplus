#pragma once

#include "game/hole_data.h"
#include "game/json_util.h"

#include <string>
#include <vector>

// Missing optional fields get these values.
inline constexpr int default_hole_par = 3;
inline constexpr tree_shape default_tree_shape{0.35f, 2.4f, 1.6f, 3.2f};

// Refused, naming the field, when the tee, pin or spline (two or more control
// points, positive width) is missing or malformed, or when an optional field,
// tree or material zone is present but malformed.
parsed<hole_data> parse_hole_from_text(const std::string& text);

// Shared authored vegetation and surface zones, in hole or world coordinates.
// An omitted array is none; one malformed entry refuses them all. A zone is
// a green, bunker, water or fairway outline ([[x, z], ...], three or more).
parsed<std::vector<material_zone>> material_zones_from_json(const json& root);
parsed<std::vector<tree_instance>> trees_from_json(const json& root);
