#pragma once

#include "game/save_data.h"

#include <optional>
#include <string>
#include <vector>

struct shop_requirement {
    std::string skill_id;
    int min_skill_level = 0;
    std::string required_unlock_id;
    std::string required_completed_course_id;
};

struct shop_item_definition {
    std::string id;
    std::string title;
    std::string description;
    int price = 0;
    std::string grant_unlock_id;
    shop_requirement requirement;
};

struct shop_definition {
    std::string id;
    std::string npc_id;
    std::string title;
    std::string subtitle;
    std::vector<shop_item_definition> items;
};

enum class shop_purchase_result {
    purchased,
    already_owned,
    insufficient_funds,
    requirements_not_met,
    invalid_item
};

std::vector<shop_definition> fallback_shop_definitions();
std::optional<shop_definition> load_shop_from_file(const std::string& path);
std::vector<shop_definition> load_shops_from_directory(const std::string& directory);

bool shop_item_owned(const save_data& save, const shop_item_definition& item);
bool shop_item_requirements_met(const save_data& save, const shop_item_definition& item);
shop_purchase_result purchase_shop_item(save_data& save, const shop_item_definition& item);
std::string shop_item_status_label(const save_data& save, const shop_item_definition& item);
