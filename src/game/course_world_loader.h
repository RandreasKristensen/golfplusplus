#pragma once

#include "game/course_definition.h"
#include "game/course_world_definition.h"

#include <optional>
#include <string>

// Missing optional fields get these values.
inline constexpr float default_hole_start_radius = 4.0f;
inline constexpr float default_collectible_radius = 2.5f;
inline constexpr float default_cart_road_width = 4.0f;

// nullopt unless the world has exactly one valid hole start per hole of
// `course`. hole_starts come back sorted by hole_index.
std::optional<course_world_definition> parse_course_world_from_text(const std::string& text,
                                                                    const course_definition& course);
std::optional<course_world_definition> load_course_world_from_file(const std::string& path,
                                                                   const course_definition& course);
// Empty when the course has no world.
std::string course_world_file_path(const std::string& asset_root, const course_definition& course);
