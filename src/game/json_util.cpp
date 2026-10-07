#include "game/json_util.h"

#include <algorithm>

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
    return value->get<int>();
}

std::optional<std::uint32_t> json_uint32(const json& object, const char* key) {
    const json* value = member(object, key);
    if (value == nullptr || !value->is_number_unsigned()) {
        return std::nullopt;
    }
    return value->get<std::uint32_t>();
}

std::optional<float> json_float(const json& object, const char* key) {
    const json* value = member(object, key);
    if (value == nullptr || !value->is_number()) {
        return std::nullopt;
    }
    return value->get<float>();
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
    if (value == nullptr || !value->is_array() || value->size() != 2 || !(*value)[0].is_number() ||
        !(*value)[1].is_number()) {
        return std::nullopt;
    }
    return glm::vec2((*value)[0].get<float>(), (*value)[1].get<float>());
}

std::optional<glm::vec3> json_vec3(const json& value) {
    if (!value.is_array() || value.size() != 3) {
        return std::nullopt;
    }
    for (const json& component : value) {
        if (!component.is_number()) {
            return std::nullopt;
        }
    }
    return glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
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

