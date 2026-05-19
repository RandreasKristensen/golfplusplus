#include "game/club_loader.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

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

std::optional<float> float_at(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number()) {
        return std::nullopt;
    }
    return it->get<float>();
}

std::optional<club_definition> parse_club_definition(const json& root) {
    if (!root.is_object()) {
        return std::nullopt;
    }

    const auto stats_it = root.find("stats");
    if (stats_it == root.end() || !stats_it->is_object()) {
        return std::nullopt;
    }

    const std::optional<float> power = float_at(*stats_it, "power");
    const std::optional<float> loft_degrees = float_at(*stats_it, "loft_degrees");
    const std::optional<float> accuracy = float_at(*stats_it, "accuracy");
    const std::optional<float> spin_bias = float_at(*stats_it, "spin_bias");
    if (!power || !loft_degrees || !accuracy || !spin_bias) {
        return std::nullopt;
    }

    club_definition club;
    club.id = string_at(root, "id").value_or("");
    club.name = string_at(root, "name").value_or(club.id);
    club.label = string_at(root, "label").value_or(club.name);
    club.price = int_at(root, "price").value_or(0);
    club.bag_order = int_at(root, "bag_order").value_or(0);
    club.stats.power = *power;
    club.stats.loft_degrees = *loft_degrees;
    club.stats.accuracy = *accuracy;
    club.stats.spin_bias = *spin_bias;
    club.stats.timing_speed = float_at(*stats_it, "timing_speed").value_or(1.0f);
    club.stats.roll_friction_scale = float_at(*stats_it, "roll_friction_scale").value_or(1.0f);
    return club;
}
}

std::vector<club_definition> load_clubs_from_directory(const std::string& directory) {
    std::vector<club_definition> clubs;
    const std::filesystem::path dir(directory);
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
        return clubs;
    }

    std::vector<std::filesystem::path> files;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());
    for (const std::filesystem::path& path : files) {
        const std::optional<json> root = load_json_file(path);
        if (!root) {
            continue;
        }
        const std::optional<club_definition> club = parse_club_definition(*root);
        if (club) {
            clubs.push_back(*club);
        }
    }

    std::sort(clubs.begin(), clubs.end(), [](const club_definition& a, const club_definition& b) {
        if (a.bag_order == b.bag_order) {
            return a.id < b.id;
        }
        return a.bag_order < b.bag_order;
    });

    return clubs;
}
