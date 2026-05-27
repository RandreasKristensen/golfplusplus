#pragma once

#include "game/course_definition.h"
#include "game/course_world_definition.h"
#include "game/save_data.h"

#include <optional>
#include <string>

std::optional<course_world_definition> load_course_world_from_file(const std::string& path,
                                                                   const course_definition& course);
std::string course_world_file_path(const std::string& asset_root, const course_definition& course);
bool shortcut_unlocked(const save_data& save, const course_world_path& shortcut);
bool collectible_available(const save_data& save,
                           const course_world_collectible& collectible,
                           int current_hole_index);
bool apply_collectible_reward(save_data& save,
                              const course_world_collectible& collectible,
                              int current_hole_index);
