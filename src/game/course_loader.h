#pragma once

#include "game/course_definition.h"

#include <optional>
#include <string>
#include <vector>

std::optional<course_definition> load_course_from_file(const std::string& path);
std::vector<course_definition> load_courses_from_directory(const std::string& directory);
course_definition fallback_course_definition();
course_definition default_course_definition(const std::vector<course_definition>& courses);
std::string course_hole_path(const std::string& asset_root,
                             const course_definition& course,
                             std::size_t hole_index);
