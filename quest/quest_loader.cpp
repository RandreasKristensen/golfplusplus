#include "quest/quest_loader.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace {
using json = nlohmann::json;

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

std::optional<quest_definition> quest_from_json(const json& root) {
    if (!root.is_object()) {
        return std::nullopt;
    }

    const auto steps_it = root.find("steps");
    if (steps_it == root.end() || !steps_it->is_array() || steps_it->empty()) {
        return std::nullopt;
    }

    quest_definition quest;
    quest.id = string_at(root, "id").value_or("");
    quest.title = string_at(root, "title").value_or(quest.id);
    if (quest.id.empty()) {
        return std::nullopt;
    }

    for (const json& step_value : *steps_it) {
        if (!step_value.is_object()) {
            return std::nullopt;
        }

        quest_step step;
        step.id = string_at(step_value, "id").value_or("");
        step.text = string_at(step_value, "text").value_or("");
        if (step.id.empty()) {
            return std::nullopt;
        }

        const auto choices_it = step_value.find("choices");
        if (choices_it != step_value.end() && choices_it->is_array()) {
            for (const json& choice_value : *choices_it) {
                if (!choice_value.is_object()) {
                    return std::nullopt;
                }
                quest_choice choice;
                choice.label = string_at(choice_value, "label").value_or("");
                choice.next = string_at(choice_value, "next").value_or("");
                if (choice.label.empty() || choice.next.empty()) {
                    return std::nullopt;
                }
                step.choices.push_back(choice);
            }
        }
        quest.steps.push_back(step);
    }

    const auto reward_it = root.find("reward");
    if (reward_it != root.end() && reward_it->is_object()) {
        quest.reward.money = int_at(*reward_it, "money").value_or(0);
        const auto unlock_it = reward_it->find("unlock");
        if (unlock_it != reward_it->end() && unlock_it->is_string()) {
            const std::string unlock = unlock_it->get<std::string>();
            if (!unlock.empty()) {
                quest.reward.unlock = unlock;
            }
        }
    }

    return quest;
}
}

std::optional<quest_definition> load_quest_from_file(const std::string& path) {
    const std::optional<json> root = load_json_file(path);
    if (!root) {
        return std::nullopt;
    }
    return quest_from_json(*root);
}

std::vector<quest_definition> load_quests_from_directory(const std::string& directory) {
    std::vector<quest_definition> quests;
    const std::filesystem::path dir(directory);
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
        return quests;
    }

    std::vector<std::filesystem::path> files;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());
    for (const std::filesystem::path& path : files) {
        const std::optional<quest_definition> quest = load_quest_from_file(path.string());
        if (quest) {
            quests.push_back(*quest);
        }
    }
    return quests;
}
