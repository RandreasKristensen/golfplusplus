#pragma once

// Shared JSON and file helpers for every loader. None of them throw: a
// missing key, a wrong type or an unreadable file gives nullopt (or an empty
// result), and the caller decides whether that is an error.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::optional<std::string> read_text_file(const std::filesystem::path& path);

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

// [x, y, z] of numbers.
std::optional<glm::vec3> json_vec3(const json& value);
std::optional<std::vector<glm::vec3>> json_vec3_array(const json& value);
std::optional<glm::vec3> json_vec3(const json& object, const char* key);
// The string elements of an array member; other elements are skipped.
std::vector<std::string> json_string_array(const json& object, const char* key);

// Every *.json file directly in `directory`, sorted by path. Empty when the
// directory is missing or unreadable.
std::vector<std::filesystem::path> json_files_in_directory(const std::filesystem::path& directory);
