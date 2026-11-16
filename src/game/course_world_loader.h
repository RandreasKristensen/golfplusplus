#pragma once

#include "game/course_definition.h"
#include "game/course_world_definition.h"
#include "game/json_util.h"

#include <string>

// Missing optional fields get these values.
inline constexpr float default_hole_start_radius = 4.0f;
inline constexpr float default_collectible_radius = 2.5f;
inline constexpr float default_cart_road_width = 4.0f;

// Refused, naming the field, unless the world has exactly one valid hole
// start per hole of `course`. hole_starts come back sorted by hole_index.
parsed<course_world_definition> parse_course_world_from_text(const std::string& text, const course_definition& course);

// Shared land/props/path loader. Hubs supply no course or hole starts.
// Omitted optional arrays and fields are none or their defaults; anything
// present but malformed refuses the world.
parsed<course_world_definition> parse_world_from_text(const std::string& text, const course_definition* course = nullptr);
