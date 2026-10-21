#pragma once

#include "game/course_definition.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// Where the courses are under the asset root: a folder each, holding the
// manifest course_file_name beside its world, holes and backdrops.
inline constexpr const char* courses_path = "world/courses";
inline constexpr const char* course_file_name = "course.json";

// nullopt when "holes" is missing, empty or holds anything but non-empty
// strings, or "backdrop" lacks either image or a haze field (amount below
// zero, distance not above it), or "sky" lacks a "MM-DD" "date" or a
// "utc_offset_hours" within -12..14, or has a "lowest_sun_deg" outside -90..30.
// Its file references are as written.
std::optional<course_definition> parse_course_from_text(const std::string& text);
// `course` read from the manifest in `folder` (relative to the asset root):
// its holes, world and backdrops made relative to the asset root.
course_definition course_in_folder(course_definition course, const std::string& folder);
// The folder of the course manifest at `path` (relative to the asset root,
// "/" separated): "world/courses/x" for "world/courses/x/course.json";
// nullopt for any other file.
std::optional<std::string> course_folder_of(const std::string& path);
// Hole `hole_index`'s file: holes/<id>.json for a bare id, else the
// reference as written. Empty when out of range.
std::string course_hole_reference(const course_definition& course, std::size_t hole_index);
