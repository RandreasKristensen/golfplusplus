#pragma once

#include "game/course_definition.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// nullopt when "holes" is missing, empty or holds anything but non-empty
// strings, or "backdrop" is missing or empty.
std::optional<course_definition> parse_course_from_text(const std::string& text);
std::optional<course_definition> load_course_from_file(const std::string& path);
// Every valid course in `directory`, sorted by file name.
std::vector<course_definition> load_courses_from_directory(const std::string& directory);
// File path of hole `hole_index`, or empty when the index is out of range.
std::string course_hole_path(const std::string& asset_root, const course_definition& course, std::size_t hole_index);
