#pragma once

#include "game/hole_data.h"

#include <optional>
#include <string>

std::optional<hole_data> load_hole_from_file(const std::string& path);
float estimate_course_extent(const hole_data& hole);
