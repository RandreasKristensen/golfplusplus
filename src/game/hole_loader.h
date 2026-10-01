#pragma once

#include "game/hole_data.h"

#include <optional>
#include <string>

// Missing optional fields get these values.
inline constexpr int default_hole_par = 3;
inline constexpr std::uint32_t default_wind_seed = 42U;
inline constexpr tree_shape default_tree_shape{0.35f, 2.4f, 1.6f, 3.2f};

// nullopt when the tee, pin or spline (two or more control points, positive
// width) is missing or malformed.
std::optional<hole_data> parse_hole_from_text(const std::string& text);
std::optional<hole_data> load_hole_from_file(const std::string& path);
