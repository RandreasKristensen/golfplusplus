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

std::optional<course_air> parse_air(const json& root) {
    const json* object = json_object(root, "air");
    if (object == nullptr) {
        return std::nullopt;
    }
    const std::optional<float> elevation = json_float(*object, "elevation_meters");
    const std::optional<float> mean = json_float(*object, "mean_temperature_c");
    const std::optional<float> range = json_float(*object, "daily_temperature_range_c");
    if (!elevation || !mean || !range || *range < 0.0f) {
        return std::nullopt;
    }
    return course_air{*elevation, *mean, *range};
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
    const std::optional<course_air> air = parse_air(*root);
    if (holes == nullptr || holes->empty() || !backdrop || !sky || !air) {
        return std::nullopt;
    }

    const std::optional<std::string> id = json_string(*root, "id");
    const std::optional<std::string> name = json_string(*root, "name");
    const std::optional<std::string> world = json_string(*root, "world");
    const std::optional<float> stimp = json_float(*root, "green_stimp_feet");
    if (!id || id->empty() || !name || !world || world->empty() || !stimp || *stimp <= 0.0f) {
        return std::nullopt;
    }
    course_definition course;
    course.id = *id;
    course.name = *name;
    course.world = *world;
    course.backdrop = *backdrop;
    course.sky = *sky;
    course.air = *air;
    course.green_stimp_feet = *stimp;
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

course_definition course_in_folder(course_definition course, const std::string& folder) {
    const auto in_folder = [&folder](const std::string& reference) {
        return folder.empty() ? reference : folder + "/" + reference;
    };
    for (std::size_t i = 0; i < course.holes.size(); ++i) {
        course.holes[i] = in_folder(course_hole_reference(course, i));
    }
    course.world = in_folder(course.world);
    course.backdrop.sky = in_folder(course.backdrop.sky);
    course.backdrop.land = in_folder(course.backdrop.land);
    return course;
}

std::optional<std::string> course_folder_of(const std::string& path) {
    const std::string prefix = std::string(courses_path) + "/";
    const std::string suffix = std::string("/") + course_file_name;
    if (path.size() <= prefix.size() + suffix.size() || path.compare(0, prefix.size(), prefix) != 0 ||
        path.compare(path.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return std::nullopt;
    }
    std::string folder = path.substr(0, path.size() - suffix.size());
    if (folder.find('/', prefix.size()) != std::string::npos) {
        return std::nullopt;
    }
    return folder;
}
