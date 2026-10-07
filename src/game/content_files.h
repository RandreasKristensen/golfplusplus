#pragma once

// Reading and writing files on disk: the one home for file I/O in game code. Every
// loader's parse_*_from_text takes text, so the server module (which has no
// filesystem) parses the same content from its embedded copy; these
// functions feed the game the same text from assets/. None of them throw: an
// unreadable file gives nullopt (or an empty result).

#include "game/club_definition.h"
#include "game/course_definition.h"
#include "game/course_world_definition.h"
#include "game/hole_data.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// The whole file, byte for byte (binary files too).
std::optional<std::string> read_text_file(const std::filesystem::path& path);

// Writes `text` to `path` (creating its directory) through a temporary
// file, so a crash never leaves a half-written file. False on failure.
bool replace_text_file(const std::filesystem::path& path, const std::string& text);

// Every *.json file directly in `directory`, sorted by path. Empty when the
// directory is missing or unreadable.
std::vector<std::filesystem::path> json_files_in_directory(const std::filesystem::path& directory);

std::optional<hole_data> load_hole_from_file(const std::string& path);

// Every valid club in `directory`, in bag order (parse_clubs_from_texts).
std::vector<club_definition> load_clubs_from_directory(const std::string& directory);

std::optional<course_definition> load_course_from_file(const std::string& path);
// Every valid course in `directory`, sorted by file name.
std::vector<course_definition> load_courses_from_directory(const std::string& directory);

// File path of hole `hole_index` (course_hole_reference under `asset_root`),
// or empty when the index is out of range.
std::string course_hole_path(const std::string& asset_root, const course_definition& course, std::size_t hole_index);

std::optional<course_world_definition> load_course_world_from_file(const std::string& path,
                                                                   const course_definition& course);
// Empty when the course has no world.
std::string course_world_file_path(const std::string& asset_root, const course_definition& course);
