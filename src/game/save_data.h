#pragma once

#include "game/progression.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

constexpr int current_save_version = 3;

struct save_data {
    int version = current_save_version;
    int money = 0;
    std::vector<std::string> unlocked_items;
    std::vector<std::string> completed_quest_ids;
    std::vector<std::string> completed_course_ids;
    std::string current_course_id;
    int current_hole_index = 0;
    std::map<int, int> hole_scores;
    skill_progression skills = default_skill_progression();
};

save_data migrate_save_data(save_data save);
std::optional<save_data> parse_save_data(const std::string& text);
std::string save_data_to_json(const save_data& save);
std::optional<save_data> load_save_data_from_file(const std::string& path);
bool write_save_data_to_file(const std::string& path, const save_data& save);
