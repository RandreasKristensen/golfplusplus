#pragma once

#include "game/club_definition.h"

#include <optional>
#include <string>
#include <vector>

// nullopt unless id, hit_sound and stats.power/loft_degrees/backspin/side_spin are present.
std::optional<club_definition> parse_club_from_text(const std::string& text);
// Every valid club in `directory`, sorted by bag_order, then id.
std::vector<club_definition> load_clubs_from_directory(const std::string& directory);
