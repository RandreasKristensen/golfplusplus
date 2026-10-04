#include "game/content_files.h"

#include "game/club_loader.h"
#include "game/course_loader.h"
#include "game/course_world_loader.h"
#include "game/hole_loader.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace {
// `reference` under `asset_root`, unless it is already absolute.
std::string under_asset_root(const std::string& asset_root, const std::string& reference) {
    std::filesystem::path path(reference);
    if (!path.is_absolute()) {
        path = std::filesystem::path(asset_root) / path;
    }
    return path.make_preferred().string();
}
}

std::optional<std::string> read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::vector<std::filesystem::path> json_files_in_directory(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    std::filesystem::directory_iterator it(directory, error);
    if (error) {
        return files;
    }
    for (; !error && it != std::filesystem::directory_iterator(); it.increment(error)) {
        std::error_code type_error;
        if (it->is_regular_file(type_error) && it->path().extension() == ".json") {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::optional<hole_data> load_hole_from_file(const std::string& path) {
    const std::optional<std::string> text = read_text_file(path);
    return text ? parse_hole_from_text(*text) : std::nullopt;
}

std::vector<club_definition> load_clubs_from_directory(const std::string& directory) {
    std::vector<std::string> texts;
    for (const std::filesystem::path& path : json_files_in_directory(directory)) {
        if (std::optional<std::string> text = read_text_file(path)) {
            texts.push_back(std::move(*text));
        }
    }
    return parse_clubs_from_texts(texts);
}

std::optional<course_definition> load_course_from_file(const std::string& path) {
    const std::optional<std::string> text = read_text_file(path);
    return text ? parse_course_from_text(*text) : std::nullopt;
}

std::vector<course_definition> load_courses_from_directory(const std::string& directory) {
    std::vector<course_definition> courses;
    for (const std::filesystem::path& path : json_files_in_directory(directory)) {
        if (std::optional<course_definition> course = load_course_from_file(path.string())) {
            courses.push_back(std::move(*course));
        }
    }
    return courses;
}

std::string course_hole_path(const std::string& asset_root, const course_definition& course, const std::size_t hole_index) {
    const std::string reference = course_hole_reference(course, hole_index);
    return reference.empty() ? std::string() : under_asset_root(asset_root, reference);
}

std::optional<course_world_definition> load_course_world_from_file(const std::string& path,
                                                                   const course_definition& course) {
    const std::optional<std::string> text = read_text_file(path);
    return text ? parse_course_world_from_text(*text, course) : std::nullopt;
}

std::string course_world_file_path(const std::string& asset_root, const course_definition& course) {
    return course.world.empty() ? std::string() : under_asset_root(asset_root, course.world);
}
