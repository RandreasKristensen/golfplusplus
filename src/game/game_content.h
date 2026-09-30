#pragma once

#include "game/club_definition.h"
#include "game/course_definition.h"

#include <string>
#include <vector>

struct game_content {
    std::string asset_root;
    std::vector<club_definition> clubs;
    std::vector<course_definition> courses;
};

game_content load_game_content(const std::string& asset_root);
