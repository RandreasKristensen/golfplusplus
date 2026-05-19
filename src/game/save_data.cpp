#include "game/save_data.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {
using json = nlohmann::json;

std::optional<int> int_at(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer()) {
        return std::nullopt;
    }
    return it->get<int>();
}

std::optional<std::string> string_at(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return std::nullopt;
    }
    return it->get<std::string>();
}

std::vector<std::string> string_array_at(const json& object, const char* key) {
    std::vector<std::string> values;
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array()) {
        return values;
    }

    for (const json& value : *it) {
        if (value.is_string()) {
            values.push_back(value.get<std::string>());
        }
    }
    return values;
}

std::optional<int> parse_int_key(const std::string& key) {
    char* end = nullptr;
    const long parsed = std::strtol(key.c_str(), &end, 10);
    if (end == key.c_str() || *end != '\0') {
        return std::nullopt;
    }
    return static_cast<int>(parsed);
}
}

save_data migrate_save_data(save_data save) {
    if (save.version < 1) {
        save.version = 1;
    }
    save.version = current_save_version;
    save.money = std::max(0, save.money);
    save.current_hole_index = std::max(0, save.current_hole_index);
    return save;
}

std::optional<save_data> parse_save_data(const std::string& text) {
    json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return std::nullopt;
    }

    save_data save;
    save.version = int_at(root, "version").value_or(0);
    save.money = int_at(root, "money").value_or(0);
    save.unlocked_items = string_array_at(root, "unlocked_items");
    save.completed_quest_ids = string_array_at(root, "completed_quest_ids");
    save.current_course_id = string_at(root, "current_course_id").value_or("");
    save.current_hole_index = int_at(root, "current_hole_index").value_or(0);

    const auto scores_it = root.find("hole_scores");
    if (scores_it != root.end() && scores_it->is_object()) {
        for (auto it = scores_it->begin(); it != scores_it->end(); ++it) {
            if (!it.value().is_number_integer()) {
                continue;
            }

            const std::optional<int> hole_index = parse_int_key(it.key());
            if (hole_index) {
                save.hole_scores[*hole_index] = it.value().get<int>();
            }
        }
    }

    return migrate_save_data(save);
}

std::string save_data_to_json(const save_data& save) {
    json root = json::object();
    root["version"] = current_save_version;
    root["money"] = std::max(0, save.money);
    root["unlocked_items"] = save.unlocked_items;
    root["completed_quest_ids"] = save.completed_quest_ids;
    root["current_course_id"] = save.current_course_id;
    root["current_hole_index"] = std::max(0, save.current_hole_index);

    json scores = json::object();
    for (const auto& score : save.hole_scores) {
        scores[std::to_string(score.first)] = score.second;
    }
    root["hole_scores"] = scores;

    return root.dump(2) + "\n";
}

std::optional<save_data> load_save_data_from_file(const std::string& path) {
    std::ifstream file{std::filesystem::path(path)};
    if (!file) {
        return std::nullopt;
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    return parse_save_data(stream.str());
}

bool write_save_data_to_file(const std::string& path, const save_data& save) {
    std::ofstream file{std::filesystem::path(path)};
    if (!file) {
        return false;
    }

    file << save_data_to_json(save);
    return static_cast<bool>(file);
}
