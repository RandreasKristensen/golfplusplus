#include "game/club_loader.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace {
std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        return {};
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

std::optional<std::size_t> find_object_end(const std::string& text, const std::size_t object_begin) {
    int depth = 0;
    bool in_string = false;
    bool escaped = false;

    for (std::size_t i = object_begin; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }

        if (c == '"') {
            in_string = true;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }

    return std::nullopt;
}

std::optional<std::string> object_for_key(const std::string& text, const char* key) {
    const std::string needle = std::string("\"") + key + "\"";
    const std::size_t key_pos = text.find(needle);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }

    const std::size_t colon = text.find(':', key_pos + needle.size());
    if (colon == std::string::npos) {
        return std::nullopt;
    }

    const std::size_t object_begin = text.find('{', colon + 1);
    if (object_begin == std::string::npos) {
        return std::nullopt;
    }

    const std::optional<std::size_t> object_end = find_object_end(text, object_begin);
    if (!object_end) {
        return std::nullopt;
    }

    return text.substr(object_begin, *object_end - object_begin + 1);
}

std::optional<std::string> string_for_key(const std::string& text, const char* key) {
    const std::string needle = std::string("\"") + key + "\"";
    const std::size_t key_pos = text.find(needle);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }

    const std::size_t colon = text.find(':', key_pos + needle.size());
    if (colon == std::string::npos) {
        return std::nullopt;
    }

    const std::size_t first_quote = text.find('"', colon + 1);
    if (first_quote == std::string::npos) {
        return std::nullopt;
    }

    std::string value;
    bool escaped = false;
    for (std::size_t i = first_quote + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (escaped) {
            value.push_back(c);
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            return value;
        } else {
            value.push_back(c);
        }
    }

    return std::nullopt;
}

std::optional<float> float_for_key(const std::string& text, const char* key) {
    const std::string needle = std::string("\"") + key + "\"";
    const std::size_t key_pos = text.find(needle);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }

    const std::size_t colon = text.find(':', key_pos + needle.size());
    if (colon == std::string::npos) {
        return std::nullopt;
    }

    std::size_t begin = colon + 1;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }

    char* end = nullptr;
    const float value = std::strtof(text.c_str() + begin, &end);
    if (end == text.c_str() + begin) {
        return std::nullopt;
    }

    return value;
}

std::optional<int> int_for_key(const std::string& text, const char* key) {
    const std::optional<float> value = float_for_key(text, key);
    if (!value) {
        return std::nullopt;
    }
    return static_cast<int>(*value);
}

std::optional<club_definition> parse_club_definition(const std::string& text) {
    const std::optional<std::string> stats = object_for_key(text, "stats");
    if (!stats) {
        return std::nullopt;
    }

    const std::optional<float> power = float_for_key(*stats, "power");
    const std::optional<float> loft_degrees = float_for_key(*stats, "loft_degrees");
    const std::optional<float> accuracy = float_for_key(*stats, "accuracy");
    const std::optional<float> spin_bias = float_for_key(*stats, "spin_bias");
    if (!power || !loft_degrees || !accuracy || !spin_bias) {
        return std::nullopt;
    }

    club_definition club;
    club.id = string_for_key(text, "id").value_or("");
    club.name = string_for_key(text, "name").value_or(club.id);
    club.label = string_for_key(text, "label").value_or(club.name);
    club.price = int_for_key(text, "price").value_or(0);
    club.bag_order = int_for_key(text, "bag_order").value_or(0);
    club.stats.power = *power;
    club.stats.loft_degrees = *loft_degrees;
    club.stats.accuracy = *accuracy;
    club.stats.spin_bias = *spin_bias;
    return club;
}
}

std::vector<club_definition> load_clubs_from_directory(const std::string& directory) {
    std::vector<club_definition> clubs;
    std::filesystem::path dir(directory);
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
        const std::string contents = read_file(path);
        const std::optional<club_definition> club = parse_club_definition(contents);
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
