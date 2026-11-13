#include "game/json_util.h"

#include <algorithm>
#include <cmath>
#include <limits>

std::optional<json> parse_json(const std::string& text) {
    json parsed = json::parse(text, nullptr, false);
    if (parsed.is_discarded()) {
        return std::nullopt;
    }
    return parsed;
}

namespace {
const json* member(const json& object, const char* key) {
    if (!object.is_object()) {
        return nullptr;
    }
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &*it;
}

std::optional<float> number_as_float(const json& value) {
    if (!value.is_number()) {
        return std::nullopt;
    }
    const double number = value.get<double>();
    if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max()) {
        return std::nullopt;
    }
    return value.get<float>();
}
}

std::optional<std::string> json_string(const json& object, const char* key) {
    const json* value = member(object, key);
    if (value == nullptr || !value->is_string()) {
        return std::nullopt;
    }
    return value->get<std::string>();
}

std::optional<int> json_int(const json& object, const char* key) {
    const json* value = member(object, key);
    if (value == nullptr || !value->is_number_integer()) {
        return std::nullopt;
    }
    if (value->is_number_unsigned()) {
        const auto number = value->get<std::uint64_t>();
        return number <= static_cast<std::uint64_t>(std::numeric_limits<int>::max())
            ? std::optional<int>(static_cast<int>(number)) : std::nullopt;
    }
    const auto number = value->get<std::int64_t>();
    return number >= std::numeric_limits<int>::min() && number <= std::numeric_limits<int>::max()
        ? std::optional<int>(static_cast<int>(number)) : std::nullopt;
}

std::optional<std::uint32_t> json_uint32(const json& object, const char* key) {
    const json* value = member(object, key);
    if (value == nullptr || !value->is_number_integer()) {
        return std::nullopt;
    }
    if (!value->is_number_unsigned() && value->get<std::int64_t>() < 0) {
        return std::nullopt;
    }
    const auto number = value->get<std::uint64_t>();
    return number <= std::numeric_limits<std::uint32_t>::max()
        ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(number)) : std::nullopt;
}

std::optional<float> json_float(const json& object, const char* key) {
    const json* value = member(object, key);
    return value != nullptr ? number_as_float(*value) : std::nullopt;
}

std::optional<bool> json_bool(const json& object, const char* key) {
    const json* value = member(object, key);
    if (value == nullptr || !value->is_boolean()) {
        return std::nullopt;
    }
    return value->get<bool>();
}

const json* json_object(const json& object, const char* key) {
    const json* value = member(object, key);
    return value != nullptr && value->is_object() ? value : nullptr;
}

const json* json_array(const json& object, const char* key) {
    const json* value = member(object, key);
    return value != nullptr && value->is_array() ? value : nullptr;
}

std::optional<glm::vec2> json_vec2(const json& object, const char* key) {
    const json* value = member(object, key);
    if (value == nullptr || !value->is_array() || value->size() != 2) {
        return std::nullopt;
    }
    const auto x = number_as_float((*value)[0]);
    const auto y = number_as_float((*value)[1]);
    return x && y ? std::optional<glm::vec2>(glm::vec2(*x, *y)) : std::nullopt;
}

std::optional<glm::vec3> json_vec3(const json& value) {
    if (!value.is_array() || value.size() != 3) {
        return std::nullopt;
    }
    const auto x = number_as_float(value[0]);
    const auto y = number_as_float(value[1]);
    const auto z = number_as_float(value[2]);
    return x && y && z ? std::optional<glm::vec3>(glm::vec3(*x, *y, *z)) : std::nullopt;
}

std::optional<glm::vec3> json_vec3(const json& object, const char* key) {
    const json* value = member(object, key);
    return value == nullptr ? std::nullopt : json_vec3(*value);
}

std::optional<std::vector<glm::vec3>> json_vec3_array(const json& value) {
    if (!value.is_array()) {
        return std::nullopt;
    }
    std::vector<glm::vec3> points;
    points.reserve(value.size());
    for (const json& element : value) {
        const std::optional<glm::vec3> point = json_vec3(element);
        if (!point) {
            return std::nullopt;
        }
        points.push_back(*point);
    }
    return points;
}

std::vector<std::string> json_string_array(const json& object, const char* key) {
    std::vector<std::string> values;
    if (const json* array = json_array(object, key)) {
        for (const json& value : *array) {
            if (value.is_string()) {
                values.push_back(value.get<std::string>());
            }
        }
    }
    return values;
}


namespace {
std::optional<json> resolve_base(const std::string& name, const std::map<std::string, json>& files,
                                 const std::vector<std::string>& merged_keys, std::set<std::string>& resolving) {
    const auto file = files.find(name);
    if (file == files.end() || !file->second.is_object() || !resolving.insert(name).second) {
        return std::nullopt;
    }
    const json& own = file->second;
    const std::optional<std::string> base_name = json_string(own, "base");
    if (!base_name) {
        return own.contains("base") ? std::nullopt : std::optional<json>(own);
    }
    std::optional<json> merged = resolve_base(*base_name, files, merged_keys, resolving);
    if (!merged) {
        return std::nullopt;
    }
    const std::optional<std::string> own_kind = json_string(own, "kind");
    if (own_kind && json_string(*merged, "kind") != own_kind) {
        return std::nullopt;
    }
    for (const auto& [key, value] : own.items()) {
        if (key == "base") {
            continue;
        }
        const bool merges = std::find(merged_keys.begin(), merged_keys.end(), key) != merged_keys.end();
        if (merges && value.is_object() && merged->contains(key) && (*merged)[key].is_object()) {
            for (const auto& [member_key, member_value] : value.items()) {
                (*merged)[key][member_key] = member_value;
            }
        } else {
            (*merged)[key] = value;
        }
    }
    merged->erase("base");
    return merged;
}
}

std::optional<json> json_with_base(const std::string& name, const std::map<std::string, json>& files,
                                   const std::vector<std::string>& merged_keys) {
    std::set<std::string> resolving;
    return resolve_base(name, files, merged_keys, resolving);
}
