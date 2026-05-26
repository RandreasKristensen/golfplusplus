#include "game/shop.h"

#include "game/progression.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {
using json = nlohmann::json;

bool contains_string(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

std::optional<json> load_json_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        return std::nullopt;
    }

    json parsed = json::parse(file, nullptr, false);
    if (parsed.is_discarded()) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<std::string> string_at(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return std::nullopt;
    }
    return it->get<std::string>();
}

std::optional<int> int_at(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer()) {
        return std::nullopt;
    }
    return it->get<int>();
}

shop_requirement parse_requirement(const json& object) {
    shop_requirement requirement;
    const auto requirement_it = object.find("requirements");
    if (requirement_it == object.end() || !requirement_it->is_object()) {
        return requirement;
    }

    requirement.skill_id = string_at(*requirement_it, "skill_id").value_or("");
    requirement.min_skill_level = int_at(*requirement_it, "min_skill_level").value_or(0);
    requirement.required_unlock_id = string_at(*requirement_it, "required_unlock_id").value_or("");
    requirement.required_completed_course_id = string_at(*requirement_it, "required_completed_course_id").value_or("");
    return requirement;
}

std::optional<shop_item_definition> parse_shop_item(const json& root) {
    if (!root.is_object()) {
        return std::nullopt;
    }

    shop_item_definition item;
    item.id = string_at(root, "id").value_or("");
    item.title = string_at(root, "title").value_or(item.id);
    item.description = string_at(root, "description").value_or("");
    item.price = std::max(0, int_at(root, "price").value_or(0));
    item.grant_unlock_id = string_at(root, "grant_unlock_id").value_or(item.id);
    item.requirement = parse_requirement(root);

    if (item.id.empty() || item.grant_unlock_id.empty()) {
        return std::nullopt;
    }
    return item;
}

std::optional<shop_definition> parse_shop_definition(const json& root) {
    if (!root.is_object()) {
        return std::nullopt;
    }

    shop_definition shop;
    shop.id = string_at(root, "id").value_or("");
    shop.npc_id = string_at(root, "npc_id").value_or(shop.id);
    shop.title = string_at(root, "title").value_or(shop.id);
    shop.subtitle = string_at(root, "subtitle").value_or("");

    const auto items_it = root.find("items");
    if (items_it != root.end() && items_it->is_array()) {
        for (const json& item_json : *items_it) {
            const std::optional<shop_item_definition> item = parse_shop_item(item_json);
            if (item) {
                shop.items.push_back(*item);
            }
        }
    }

    if (shop.id.empty()) {
        return std::nullopt;
    }
    return shop;
}
}

std::vector<shop_definition> fallback_shop_definitions() {
    shop_definition starter;
    starter.id = "starter_shack";
    starter.npc_id = "starter_caddie";
    starter.title = "CADDIE SHACK";
    starter.subtitle = "COURSE UNLOCKS";
    starter.items = {
        shop_item_definition{"marienlyst_day_pass", "DAY PASS", "Unlocks the local pro counter.", 0, "marienlyst_day_pass", shop_requirement{}},
        shop_item_definition{"yardage_book", "YARDAGE BOOK", "A bought unlock gated by the day pass.", 25, "yardage_book", shop_requirement{"", 0, "marienlyst_day_pass", ""}},
        shop_item_definition{"rangefinder", "RANGEFINDER", "Hold shift while walking to read distance to the pin.", 35, rangefinder_unlock_id(), shop_requirement{"", 0, "marienlyst_day_pass", ""}},
        shop_item_definition{"cart", "GOLF CART", "Hold left shift while walking to drive the cart.", 80, cart_unlock_id(), shop_requirement{"", 0, "marienlyst_day_pass", ""}},
        shop_item_definition{"clubhouse_pin", "CLUBHOUSE PIN", "A course-clear souvenir unlock.", 40, "clubhouse_pin", shop_requirement{"", 0, "", "course_01"}}
    };

    shop_definition range;
    range.id = "range_rat";
    range.npc_id = "range_rat";
    range.title = "RANGE RAT";
    range.subtitle = "LEVEL UNLOCKS";
    range.items = {
        shop_item_definition{"swing_notebook", "SWING NOTEBOOK", "Available once golf swing reaches level 2.", 15, "swing_notebook", shop_requirement{golf_swing_skill_id(), 2, "", ""}},
        shop_item_definition{"smoke_trick_zine", "SMOKE ZINE", "Available once smoking reaches level 2.", 10, "smoke_trick_zine", shop_requirement{smoking_skill_id(), 2, "", ""}},
        shop_item_definition{"cigarette_filterless", "FILTERLESS PACK", "Unlocks smoke breaks with a short steadier-strike effect.", 5, cigarette_filterless_unlock_id(), shop_requirement{}},
        shop_item_definition{"cigarette_menthol", "MENTHOL PACK", "A cleaner drag that briefly slows swing timing.", 15, cigarette_menthol_unlock_id(), shop_requirement{smoking_skill_id(), 2, "", ""}},
        shop_item_definition{"cigarette_longcut", "LONGCUT PACK", "A longer smoke break with a mild combined effect.", 30, cigarette_longcut_unlock_id(), shop_requirement{smoking_skill_id(), 4, "", ""}}
    };

    shop_definition pro;
    pro.id = "pro_counter";
    pro.npc_id = "club_pro";
    pro.title = "PRO COUNTER";
    pro.subtitle = "CLUB UNLOCKS";
    pro.items = {
        shop_item_definition{"sand_wedge", "SAND WEDGE", "Adds the sand wedge to your bag.", 20, "sand_wedge", shop_requirement{golf_swing_skill_id(), 2, "", ""}},
        shop_item_definition{"nine_iron", "9 IRON", "Adds the 9 iron to your bag.", 30, "nine_iron", shop_requirement{golf_swing_skill_id(), 2, "", ""}},
        shop_item_definition{"five_iron", "5 IRON", "Adds the 5 iron to your bag.", 45, "five_iron", shop_requirement{golf_swing_skill_id(), 3, "", ""}},
        shop_item_definition{"seven_wood", "7 WOOD", "Adds the 7 wood to your bag.", 65, "seven_wood", shop_requirement{golf_swing_skill_id(), 4, "", ""}},
        shop_item_definition{"four_wood", "4 WOOD", "Adds the 4 wood to your bag.", 80, "four_wood", shop_requirement{golf_swing_skill_id(), 5, "", ""}},
        shop_item_definition{"driver", "DRIVER", "Adds the driver to your bag.", 110, "driver", shop_requirement{golf_swing_skill_id(), 6, "", ""}}
    };

    return {starter, range, pro};
}

std::optional<shop_definition> load_shop_from_file(const std::string& path) {
    const std::optional<json> root = load_json_file(std::filesystem::path(path));
    if (!root) {
        return std::nullopt;
    }
    return parse_shop_definition(*root);
}

std::vector<shop_definition> load_shops_from_directory(const std::string& directory) {
    std::vector<shop_definition> shops;
    const std::filesystem::path dir(directory);
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
        return shops;
    }

    std::vector<std::filesystem::path> files;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());
    for (const std::filesystem::path& path : files) {
        const std::optional<shop_definition> shop = load_shop_from_file(path.string());
        if (shop) {
            shops.push_back(*shop);
        }
    }
    return shops;
}

bool shop_item_owned(const save_data& save, const shop_item_definition& item) {
    return contains_string(save.unlocked_items, item.grant_unlock_id);
}

bool shop_item_requirements_met(const save_data& save, const shop_item_definition& item) {
    const shop_requirement& requirement = item.requirement;
    if (!requirement.skill_id.empty() &&
        skill_level(skill_xp(save.skills, requirement.skill_id)) < std::max(1, requirement.min_skill_level)) {
        return false;
    }
    if (!requirement.required_unlock_id.empty() &&
        !contains_string(save.unlocked_items, requirement.required_unlock_id)) {
        return false;
    }
    if (!requirement.required_completed_course_id.empty() &&
        !contains_string(save.completed_course_ids, requirement.required_completed_course_id)) {
        return false;
    }
    return true;
}

shop_purchase_result purchase_shop_item(save_data& save, const shop_item_definition& item) {
    if (item.id.empty() || item.grant_unlock_id.empty()) {
        return shop_purchase_result::invalid_item;
    }
    if (shop_item_owned(save, item)) {
        return shop_purchase_result::already_owned;
    }
    if (!shop_item_requirements_met(save, item)) {
        return shop_purchase_result::requirements_not_met;
    }
    if (save.money < item.price) {
        return shop_purchase_result::insufficient_funds;
    }

    save.money = std::max(0, save.money - item.price);
    save.unlocked_items.push_back(item.grant_unlock_id);
    return shop_purchase_result::purchased;
}

std::string shop_item_status_label(const save_data& save, const shop_item_definition& item) {
    if (shop_item_owned(save, item)) {
        return "OWNED";
    }
    if (!shop_item_requirements_met(save, item)) {
        return "LOCKED";
    }
    if (save.money < item.price) {
        return "NEED GOLD";
    }
    return std::to_string(std::max(0, item.price)) + " GOLD";
}
