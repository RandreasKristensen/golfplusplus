#include "game/course_loader.h"

#include "game/json_util.h"


std::optional<course_definition> parse_course_from_text(const std::string& text) {
    const std::optional<json> root = parse_json(text);
    if (!root || !root->is_object()) {
        return std::nullopt;
    }

    const json* holes = json_array(*root, "holes");
    const std::optional<std::string> backdrop = json_string(*root, "backdrop");
    if (holes == nullptr || holes->empty() || !backdrop || backdrop->empty()) {
        return std::nullopt;
    }

    course_definition course;
    course.id = json_string(*root, "id").value_or("");
    course.name = json_string(*root, "name").value_or(course.id);
    course.world = json_string(*root, "world").value_or("");
    course.backdrop = *backdrop;
    for (const json& hole : *holes) {
        if (!hole.is_string() || hole.get<std::string>().empty()) {
            return std::nullopt;
        }
        course.holes.push_back(hole.get<std::string>());
    }
    return course;
}

std::string course_hole_reference(const course_definition& course, const std::size_t hole_index) {
    if (hole_index >= course.holes.size()) {
        return {};
    }
    const std::string& reference = course.holes[hole_index];
    const bool bare_id = reference.find_first_of("/\\.") == std::string::npos;
    return bare_id ? "holes/" + reference + ".json" : reference;
}
