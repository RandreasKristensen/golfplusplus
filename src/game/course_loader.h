#pragma once

#include "game/course_definition.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// nullopt when "holes" is missing, empty or holds anything but non-empty
// strings, or "backdrop" is missing or empty.
std::optional<course_definition> parse_course_from_text(const std::string& text);
// Hole `hole_index`'s file relative to the asset root: holes/<id>.json for a
// bare id, else the reference as written. Empty when out of range.
std::string course_hole_reference(const course_definition& course, std::size_t hole_index);
