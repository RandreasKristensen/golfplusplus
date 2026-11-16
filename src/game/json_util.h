#pragma once

// Shared JSON helpers for every loader. None of them throw: a missing key or
// a wrong type gives nullopt (or an empty result), and the caller decides
// whether that is an error. Reading files is game/content_files.h.
// Numeric reads also refuse values outside the destination's finite range.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Content read from JSON, or nullopt and what is wrong with it, from the
// field it is in ("trees[2].leaf_radius: ...").
template <class T>
struct parsed {
    std::optional<T> value;
    std::string error;
};

// Parses JSON text; nullopt when it is not valid JSON.
std::optional<json> parse_json(const std::string& text);

std::optional<std::string> json_string(const json& object, const char* key);
std::optional<int> json_int(const json& object, const char* key);
std::optional<std::uint32_t> json_uint32(const json& object, const char* key);
std::optional<float> json_float(const json& object, const char* key);
std::optional<bool> json_bool(const json& object, const char* key);
// An optional member: `fallback` when it is omitted, nullopt when it is
// present but malformed.
std::optional<int> json_int_or(const json& object, const char* key, int fallback);
std::optional<float> json_float_or(const json& object, const char* key, float fallback);
std::optional<bool> json_bool_or(const json& object, const char* key, bool fallback);
// The same checked reads of a value itself, such as an array element.
std::optional<int> json_int(const json& value);
std::optional<std::int64_t> json_int64(const json& value);
std::optional<float> json_float(const json& value);
// The member as an object or array, or nullptr when missing or another type.
const json* json_object(const json& object, const char* key);
const json* json_array(const json& object, const char* key);

// [x, y] of numbers.
std::optional<glm::vec2> json_vec2(const json& value);
std::optional<glm::vec2> json_vec2(const json& object, const char* key);
// [x, y, z] of numbers.
std::optional<glm::vec3> json_vec3(const json& value);
std::optional<std::vector<glm::vec3>> json_vec3_array(const json& value);
std::optional<glm::vec3> json_vec3(const json& object, const char* key);
// Every element of the optional array member `key`, each read by `read`
// (json -> parsed<T>): none when it is omitted, nullopt when it or any
// element is malformed, the error naming the element ("trees[2].").
template <class T, class Read>
parsed<std::vector<T>> json_elements(const json& object, const char* key, const Read& read) {
    std::vector<T> elements;
    const auto found = object.find(key);
    if (found == object.end()) {
        return {elements, ""};
    }
    const json& array = *found;
    if (!array.is_array()) {
        return {std::nullopt, std::string(key) + ": not an array"};
    }
    for (std::size_t i = 0; i < array.size(); ++i) {
        parsed<T> element = read(array[i]);
        if (!element.value) {
            return {std::nullopt, std::string(key) + "[" + std::to_string(i) + "]." + element.error};
        }
        elements.push_back(std::move(*element.value));
    }
    return {std::move(elements), ""};
}

// The string elements of an array member; other elements are skipped.
std::vector<std::string> json_string_array(const json& object, const char* key);

// Catalog files that may name a `base` file of theirs to build on: file
// `name` of `files` (by name) laid over its base's, resolved through as
// many levels as are named. Each object member named in `merged_keys` merges
// into the base's member by member; any other member replaces the base's.
// The result has no `base`. nullopt for a missing file or base, a cycle, or
// a `kind` that differs from its base's.
std::optional<json> json_with_base(const std::string& name, const std::map<std::string, json>& files,
                                   const std::vector<std::string>& merged_keys);
