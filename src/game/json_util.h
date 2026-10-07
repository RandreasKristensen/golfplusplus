#pragma once

// Shared JSON helpers for every loader. None of them throw: a missing key or
// a wrong type gives nullopt (or an empty result), and the caller decides
// whether that is an error. Reading files is game/content_files.h.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Parses JSON text; nullopt when it is not valid JSON.
std::optional<json> parse_json(const std::string& text);

std::optional<std::string> json_string(const json& object, const char* key);
std::optional<int> json_int(const json& object, const char* key);
std::optional<std::uint32_t> json_uint32(const json& object, const char* key);
std::optional<float> json_float(const json& object, const char* key);
std::optional<bool> json_bool(const json& object, const char* key);
// The member as an object or array, or nullptr when missing or another type.
const json* json_object(const json& object, const char* key);
const json* json_array(const json& object, const char* key);

// [x, y] of numbers.
std::optional<glm::vec2> json_vec2(const json& object, const char* key);
// [x, y, z] of numbers.
std::optional<glm::vec3> json_vec3(const json& value);
std::optional<std::vector<glm::vec3>> json_vec3_array(const json& value);
std::optional<glm::vec3> json_vec3(const json& object, const char* key);
// The string elements of an array member; other elements are skipped.
std::vector<std::string> json_string_array(const json& object, const char* key);
