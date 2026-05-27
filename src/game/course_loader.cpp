#include "game/course_loader.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {
using json = nlohmann::json;

std::optional<json> load_json_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        return std::nullopt;
    }

    json parsed = json::parse(file, nullptr, false);
    if (parsed.is_discarded()) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<std::string> string_at(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return std::nullopt;
    }
    return it->get<std::string>();
}

std::optional<int> int_at(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer()) {
        return std::nullopt;
    }
    return it->get<int>();
}

std::optional<course_definition> course_from_json(const json& root) {
    if (!root.is_object()) {
        return std::nullopt;
    }

    const auto holes_it = root.find("holes");
    if (holes_it == root.end() || !holes_it->is_array() || holes_it->empty()) {
        return std::nullopt;
    }

    course_definition course;
    course.id = string_at(root, "id").value_or("");
    course.name = string_at(root, "name").value_or(course.id);
    course.world = string_at(root, "world").value_or("");
    course.hole_count = int_at(root, "hole_count").value_or(static_cast<int>(holes_it->size()));

    for (const json& hole : *holes_it) {
        if (!hole.is_string()) {
            return std::nullopt;
        }
        const std::string hole_path = hole.get<std::string>();
        if (hole_path.empty()) {
            return std::nullopt;
        }
        course.holes.push_back(hole_path);
    }

    if (course.hole_count <= 0 || course.hole_count != static_cast<int>(course.holes.size())) {
        return std::nullopt;
    }

    return course;
}

std::filesystem::path hole_reference_path(const std::string& reference) {
    const std::filesystem::path path(reference);
    if (path.is_absolute() || path.has_parent_path() || path.has_extension()) {
        return path;
    }
    return std::filesystem::path("holes") / (reference + ".json");
}
}

std::optional<course_definition> load_course_from_file(const std::string& path) {
    const std::optional<json> root = load_json_file(path);
    if (!root) {
        return std::nullopt;
    }
    return course_from_json(*root);
}

std::vector<course_definition> load_courses_from_directory(const std::string& directory) {
    std::vector<course_definition> courses;
    const std::filesystem::path dir(directory);
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
        return courses;
    }

    std::vector<std::filesystem::path> files;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());
    for (const std::filesystem::path& path : files) {
        const std::optional<course_definition> course = load_course_from_file(path.string());
        if (course) {
            courses.push_back(*course);
        }
    }

    return courses;
}

course_definition fallback_course_definition() {
    course_definition course;
    course.id = "dev_course";
    course.name = "Development Course";
    course.hole_count = 1;
    course.holes = {"holes/test.json"};
    return course;
}

course_definition default_course_definition(const std::vector<course_definition>& courses) {
    const auto dev = std::find_if(courses.begin(), courses.end(), [](const course_definition& course) {
        return course.id == "dev_course";
    });
    if (dev != courses.end()) {
        return *dev;
    }
    if (!courses.empty()) {
        return courses.front();
    }
    return fallback_course_definition();
}

std::string course_hole_path(const std::string& asset_root,
                             const course_definition& course,
                             const std::size_t hole_index) {
    if (hole_index >= course.holes.size()) {
        return {};
    }

    const std::filesystem::path hole_reference = hole_reference_path(course.holes[hole_index]);
    if (hole_reference.is_absolute()) {
        return hole_reference.string();
    }

    return (std::filesystem::path(asset_root) / hole_reference).string();
}
