#include "game/course_loader.h"

#include "game/json_util.h"

#include <filesystem>

std::optional<course_definition> parse_course_from_text(const std::string& text) {
    const std::optional<json> root = parse_json(text);
    if (!root || !root->is_object()) {
        return std::nullopt;
    }

    const json* holes = json_array(*root, "holes");
    if (holes == nullptr || holes->empty()) {
        return std::nullopt;
    }

    course_definition course;
    course.id = json_string(*root, "id").value_or("");
    course.name = json_string(*root, "name").value_or(course.id);
    course.world = json_string(*root, "world").value_or("");
    for (const json& hole : *holes) {
        if (!hole.is_string() || hole.get<std::string>().empty()) {
            return std::nullopt;
        }
        course.holes.push_back(hole.get<std::string>());
    }
    return course;
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
    if (hole_index >= course.holes.size()) {
        return {};
    }

    const std::filesystem::path reference(course.holes[hole_index]);
    if (reference.is_absolute()) {
        return reference.string();
    }
    const bool bare_id = !reference.has_parent_path() && !reference.has_extension();
    const std::filesystem::path relative = bare_id ? std::filesystem::path("holes") / (reference.string() + ".json") : reference;
    return (std::filesystem::path(asset_root) / relative).string();
}
