#pragma once

#include "quest/quest_definition.h"

#include <optional>
#include <string>
#include <vector>

std::optional<quest_definition> load_quest_from_file(const std::string& path);
std::vector<quest_definition> load_quests_from_directory(const std::string& directory);
