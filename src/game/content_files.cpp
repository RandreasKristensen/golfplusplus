#include "game/content_files.h"

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

bool write_text_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    return static_cast<bool>(file);
}

// Replaces `path` with `temp`, keeping the old file as `.bak` until the
// replacement succeeded (rename cannot overwrite an existing file everywhere).
bool replace_file(const std::filesystem::path& temp, const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path backup = path.string() + ".bak";
    const bool had_old = std::filesystem::exists(path, error);
    if (had_old) {
        std::filesystem::rename(path, backup, error);
        if (error) {
            std::filesystem::remove(temp, error);
            return false;
        }
    }

    std::filesystem::rename(temp, path, error);
    if (error) {
        std::error_code restore_error;
        if (had_old) {
            std::filesystem::rename(backup, path, restore_error);
        }
        std::filesystem::remove(temp, restore_error);
        return false;
    }

    if (had_old) {
        std::filesystem::remove(backup, error);
    }
    return true;
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

std::vector<std::filesystem::path> json_files_under(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    std::filesystem::recursive_directory_iterator it(directory, error);
    for (; !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
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

std::vector<std::string> json_texts_in_directory(const std::filesystem::path& directory) {
    std::vector<std::string> texts;
    for (const std::filesystem::path& path : json_files_in_directory(directory)) {
        if (std::optional<std::string> text = read_text_file(path)) {
            texts.push_back(std::move(*text));
        }
    }
    return texts;
}

std::optional<course_definition> load_course(const std::string& asset_root, const std::string& folder) {
    const std::optional<std::string> text =
        read_text_file(std::filesystem::path(asset_root) / folder / course_file_name);
    std::optional<course_definition> course = text ? parse_course_from_text(*text) : std::nullopt;
    return course ? std::optional<course_definition>(course_in_folder(std::move(*course), folder)) : std::nullopt;
}

std::vector<course_definition> load_courses(const std::string& asset_root) {
    std::vector<std::string> folders;
    std::error_code error;
    std::filesystem::directory_iterator it(std::filesystem::path(asset_root) / courses_path, error);
    for (; !error && it != std::filesystem::directory_iterator(); it.increment(error)) {
        std::error_code type_error;
        if (it->is_directory(type_error)) {
            folders.push_back(std::string(courses_path) + "/" + it->path().filename().string());
        }
    }
    std::sort(folders.begin(), folders.end());
    std::vector<course_definition> courses;
    for (const std::string& folder : folders) {
        if (std::optional<course_definition> course = load_course(asset_root, folder)) {
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
    return under_asset_root(asset_root, course.world);
}

bool replace_text_file(const std::filesystem::path& path, const std::string& text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }

    const std::filesystem::path temp = path.string() + ".tmp";
    if (!write_text_file(temp, text)) {
        std::filesystem::remove(temp, error);
        return false;
    }
    return replace_file(temp, path);
}

item_catalog_load_result load_items(const std::string& asset_root) {
    const std::filesystem::path root = std::filesystem::path(asset_root) / items_path;
    std::map<std::string, std::string> texts;
    for (const std::filesystem::path& path : json_files_under(root)) {
        const std::filesystem::path relative = path.lexically_relative(root);
        if (!relative.has_parent_path()) {
            continue;  // not an item: the default loadout (game/loadout.h)
        }
        const std::optional<std::string> text = read_text_file(path);
        if (!text) {
            item_catalog_load_result result;
            result.error = "cannot read " + path.string();
            return result;
        }
        texts[relative.generic_string()] = *text;
    }
    return parse_item_catalog(texts);
}
