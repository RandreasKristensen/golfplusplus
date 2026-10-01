#include "game/string_table.h"

#include <cstring>

#include <nlohmann/json.hpp>

std::optional<string_table> parse_string_table(const std::string& text) {
    using json = nlohmann::json;
    const json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return std::nullopt;
    }

    string_table table;
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (!it.value().is_string()) {
            return std::nullopt;
        }
        table.entries.emplace(it.key(), it.value().get<std::string>());
    }
    return table;
}

std::string lookup_text(const string_table& table, const char* key) {
    const auto it = table.entries.find(key);
    if (it == table.entries.end()) {
        return "#" + std::string(key) + "#";
    }
    return it->second;
}

std::string fill_placeholders(const std::string& pattern, const std::initializer_list<text_arg> args) {
    std::string result;
    result.reserve(pattern.size());
    std::size_t cursor = 0;
    while (cursor < pattern.size()) {
        const std::size_t open = pattern.find('{', cursor);
        const std::size_t close = open == std::string::npos ? std::string::npos : pattern.find('}', open);
        if (close == std::string::npos) {
            result.append(pattern, cursor, std::string::npos);
            break;
        }

        result.append(pattern, cursor, open - cursor);
        const std::size_t name_length = close - open - 1;
        const text_arg* match = nullptr;
        for (const text_arg& arg : args) {
            if (std::strlen(arg.name) == name_length && pattern.compare(open + 1, name_length, arg.name) == 0) {
                match = &arg;
                break;
            }
        }
        if (match != nullptr) {
            result += match->value;
        } else {
            result.append(pattern, open, close - open + 1);
        }
        cursor = close + 1;
    }
    return result;
}

std::string format_text(const string_table& table, const char* key, const std::initializer_list<text_arg> args) {
    return fill_placeholders(lookup_text(table, key), args);
}
