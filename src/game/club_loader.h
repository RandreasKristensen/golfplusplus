#pragma once

#include "game/club_definition.h"

#include <string>
#include <vector>

std::vector<club_definition> load_clubs_from_directory(const std::string& directory);
