#include "game/course_loader.h"

#include "game/json_util.h"

namespace {
std::optional<course_backdrop> parse_backdrop(const json& root) {
    const json* object = json_object(root, "backdrop");
    if (object == nullptr) {
        return std::nullopt;
    }
    course_backdrop backdrop;
    backdrop.sky = json_string(*object, "sky").value_or("");
    backdrop.land = json_string(*object, "land").value_or("");
    const std::optional<glm::vec3> haze_color = json_vec3(*object, "haze_color");
    const std::optional<float> haze_amount = json_float(*object, "haze_amount");
    const std::optional<float> haze_distance = json_float(*object, "haze_distance");
    if (backdrop.sky.empty() || backdrop.land.empty() || !haze_color || !haze_amount || *haze_amount < 0.0f ||
        !haze_distance || *haze_distance <= 0.0f) {
        return std::nullopt;
    }
    backdrop.haze_color = *haze_color;
    backdrop.haze_amount = *haze_amount;
    backdrop.haze_distance = *haze_distance;
    return backdrop;
}
}

std::optional<course_definition> parse_course_from_text(const std::string& text) {
    const std::optional<json> root = parse_json(text);
    if (!root || !root->is_object()) {
        return std::nullopt;
    }

    const json* holes = json_array(*root, "holes");
    const std::optional<course_backdrop> backdrop = parse_backdrop(*root);
    if (holes == nullptr || holes->empty() || !backdrop) {
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
