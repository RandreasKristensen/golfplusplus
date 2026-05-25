#pragma once

#include "game/hole_data.h"

#include <optional>
#include <string>
#include <vector>

std::optional<hole_data> load_hole_from_file(const std::string& path);
std::vector<hole_data> load_holes_from_directory(const std::string& directory);
float estimate_course_extent(const hole_data& hole);
