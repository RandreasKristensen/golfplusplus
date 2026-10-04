#pragma once

#include "game/club_definition.h"

#include <optional>
#include <string>
#include <vector>

// nullopt unless id, hit_sound and stats.power/loft_degrees/backspin/side_spin are present.
std::optional<club_definition> parse_club_from_text(const std::string& text);
// The valid clubs among `texts`, sorted by bag_order, then id.
std::vector<club_definition> parse_clubs_from_texts(const std::vector<std::string>& texts);
