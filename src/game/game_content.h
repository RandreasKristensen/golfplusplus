#pragma once

// All static content, loaded once at startup. Fails as a whole: the game does
// not run with partial content.

#include "game/club_definition.h"
#include "game/course_definition.h"
#include "game/game_tuning.h"
#include "game/reward_rules.h"

#include <optional>
#include <string>
#include <vector>

struct game_content {
    std::string asset_root;
    game_tuning tuning;
    std::vector<club_definition> clubs;      // bag order
    std::vector<course_definition> courses;  // sorted by file name
    std::vector<skill_definition> skills;    // skills panel order
    reward_rules rewards;
};

struct game_content_load_result {
    std::optional<game_content> content;
    std::string error;  // what is missing, when content is nullopt
};

game_content_load_result load_game_content(const std::string& asset_root);
