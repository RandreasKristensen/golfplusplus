#include "game/course_loader.h"

#include "game/json_util.h"
#include "game/sky_clock.h"

namespace {
// Without "lowest_sun_deg" the sun sinks as far as the latitude and date say;
// a floor is within these degrees.
constexpr float no_sun_floor_deg = -90.0f;
constexpr float highest_sun_floor_deg = 30.0f;
// The world's clocks run from 12 hours behind UTC to 14 ahead.
constexpr float earliest_utc_offset_hours = -12.0f;
constexpr float latest_utc_offset_hours = 14.0f;

std::optional<course_sky> parse_sky(const json& root) {
    const json* object = json_object(root, "sky");
    if (object == nullptr) {
        return std::nullopt;
    }
    const std::optional<std::string> date_text = json_string(*object, "date");
    const std::optional<civil_date> date = date_text ? parse_month_day(*date_text) : std::nullopt;
    const std::optional<float> utc_offset = json_float(*object, "utc_offset_hours");
    const bool has_floor = object->contains("lowest_sun_deg");
    const std::optional<float> floor = has_floor ? json_float(*object, "lowest_sun_deg") : std::optional<float>(no_sun_floor_deg);
    if (!date || !utc_offset || *utc_offset < earliest_utc_offset_hours || *utc_offset > latest_utc_offset_hours || !floor ||
        *floor < no_sun_floor_deg || *floor > highest_sun_floor_deg) {
        return std::nullopt;
    }
    course_sky sky;
    sky.month = date->month;
    sky.day = date->day;
    sky.utc_offset_hours = *utc_offset;
    sky.lowest_sun_deg = *floor;
    return sky;
}

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
    const std::optional<course_sky> sky = parse_sky(*root);
    if (holes == nullptr || holes->empty() || !backdrop || !sky) {
        return std::nullopt;
    }

    const std::optional<std::string> id = json_string(*root, "id");
    const std::optional<std::string> name = json_string(*root, "name");
    const std::optional<std::string> world = json_string(*root, "world");
    if (!id || id->empty() || !name || !world || world->empty()) {
        return std::nullopt;
    }
    course_definition course;
    course.id = *id;
    course.name = *name;
    course.world = *world;
    course.backdrop = *backdrop;
    course.sky = *sky;
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
