#include "game/course_loader.h"

#include "game/sky_clock.h"

#include <algorithm>
#include <utility>

namespace {
// Without "lowest_sun_deg" the sun sinks as far as the latitude and date say;
// a floor is within these degrees.
constexpr float no_sun_floor_deg = -90.0f;
constexpr float highest_sun_floor_deg = 30.0f;
// The world's clocks run from 12 hours behind UTC to 14 ahead.
constexpr float earliest_utc_offset_hours = -12.0f;
constexpr float latest_utc_offset_hours = 14.0f;

parsed<course_sky> parse_sky(const json& root) {
    const json* object = json_object(root, "sky");
    if (object == nullptr) {
        return {std::nullopt, "sky: missing"};
    }
    const std::optional<std::string> date_text = json_string(*object, "date");
    const std::optional<civil_date> date = date_text ? parse_month_day(*date_text) : std::nullopt;
    if (!date) {
        return {std::nullopt, "sky.date: not \"MM-DD\""};
    }
    const std::optional<float> utc_offset = json_float(*object, "utc_offset_hours");
    if (!utc_offset || *utc_offset < earliest_utc_offset_hours || *utc_offset > latest_utc_offset_hours) {
        return {std::nullopt, "sky.utc_offset_hours: not within -12..14"};
    }
    const std::optional<float> shift = json_float_or(*object, "clock_shift_hours", 0.0f);
    if (!shift || *shift < 0.0f || *shift >= 24.0f) {
        return {std::nullopt, "sky.clock_shift_hours: not within 0..24"};
    }
    const std::optional<float> floor = json_float_or(*object, "lowest_sun_deg", no_sun_floor_deg);
    if (!floor || *floor < no_sun_floor_deg || *floor > highest_sun_floor_deg) {
        return {std::nullopt, "sky.lowest_sun_deg: not within -90..30"};
    }
    course_sky sky;
    sky.month = date->month;
    sky.day = date->day;
    sky.utc_offset_hours = *utc_offset;
    sky.clock_shift_hours = *shift;
    sky.lowest_sun_deg = *floor;
    return {sky, ""};
}

parsed<course_air> parse_air(const json& root) {
    const json* object = json_object(root, "air");
    if (object == nullptr) {
        return {std::nullopt, "air: missing"};
    }
    const std::optional<float> elevation = json_float(*object, "elevation_meters");
    const std::optional<float> mean = json_float(*object, "mean_temperature_c");
    const std::optional<float> range = json_float(*object, "daily_temperature_range_c");
    if (!elevation || !mean || !range || *range < 0.0f) {
        return {std::nullopt, !elevation ? "air.elevation_meters: not a number"
                              : !mean    ? "air.mean_temperature_c: not a number"
                                         : "air.daily_temperature_range_c: not a number of at least 0"};
    }
    return {course_air{*elevation, *mean, *range}, ""};
}

parsed<course_backdrop> parse_backdrop(const json& root) {
    const json* object = json_object(root, "backdrop");
    if (object == nullptr) {
        return {std::nullopt, "backdrop: missing"};
    }
    course_backdrop backdrop;
    backdrop.sky = json_string(*object, "sky").value_or("");
    backdrop.land = json_string(*object, "land").value_or("");
    if (backdrop.sky.empty() || backdrop.land.empty()) {
        return {std::nullopt, backdrop.sky.empty() ? "backdrop.sky: not a file" : "backdrop.land: not a file"};
    }
    const std::optional<glm::vec3> haze_color = json_vec3(*object, "haze_color");
    const std::optional<float> haze_amount = json_float(*object, "haze_amount");
    const std::optional<float> haze_distance = json_float(*object, "haze_distance");
    if (!haze_color || !haze_amount || *haze_amount < 0.0f || !haze_distance || *haze_distance <= 0.0f) {
        return {std::nullopt, !haze_color                       ? "backdrop.haze_color: not [r, g, b]"
                              : !haze_amount || *haze_amount < 0 ? "backdrop.haze_amount: not a number of at least 0"
                                                                 : "backdrop.haze_distance: not a number above 0"};
    }
    backdrop.haze_color = *haze_color;
    backdrop.haze_amount = *haze_amount;
    backdrop.haze_distance = *haze_distance;
    return {backdrop, ""};
}
}

parsed<place_definition> parse_place_from_text(const std::string& text) {
    const auto root = parse_json(text);
    if (!root || !root->is_object()) {
        return {std::nullopt, "not a JSON object"};
    }
    const parsed<course_backdrop> backdrop = parse_backdrop(*root);
    const parsed<course_sky> sky = parse_sky(*root);
    const parsed<course_air> air = parse_air(*root);
    if (!backdrop.value || !sky.value || !air.value) {
        return {std::nullopt, !backdrop.value ? backdrop.error : !sky.value ? sky.error : air.error};
    }

    const std::optional<std::string> id = json_string(*root, "id");
    const std::optional<std::string> name = json_string(*root, "name");
    const std::optional<std::string> world = json_string(*root, "world");
    const std::optional<float> stimp = json_float(*root, "green_stimp_feet");
    const std::optional<bool> carts = json_bool_or(*root, "carts", true);
    if (!id || id->empty() || !name || !world || world->empty() || !stimp || *stimp <= 0.0f || !carts) {
        return {std::nullopt, !id || id->empty() ? "id: not a name"
                              : !name            ? "name: not text"
                              : !world || world->empty() ? "world: not a file"
                              : !carts                   ? "carts: not true or false"
                                                         : "green_stimp_feet: not a speed above 0"};
    }
    place_definition course;
    course.id = *id;
    course.name = *name;
    course.world = *world;
    course.backdrop = *backdrop.value;
    course.sky = *sky.value;
    course.air = *air.value;
    course.green_stimp_feet = *stimp;
    course.carts = *carts;
    return {course, ""};
}

parsed<course_definition> parse_course_from_text(const std::string& text) {
    const std::optional<json> root = parse_json(text);
    if (!root || !root->is_object()) {
        return {std::nullopt, "not a JSON object"};
    }
    if (root->contains("practice")) {
        return {std::nullopt, "practice: a course has holes, not practice"};
    }
    const json* holes = json_array(*root, "holes");
    if (holes == nullptr || holes->empty()) {
        return {std::nullopt, "holes: not one or more hole files"};
    }
    const parsed<place_definition> environment = parse_place_from_text(text);
    if (!environment.value) {
        return {std::nullopt, environment.error};
    }
    course_definition course;
    static_cast<place_definition&>(course) = *environment.value;
    for (std::size_t i = 0; i < holes->size(); ++i) {
        const json& hole = (*holes)[i];
        if (!hole.is_string() || hole.get<std::string>().empty()) {
            return {std::nullopt, "holes[" + std::to_string(i) + "]: not a hole file"};
        }
        course.holes.push_back(hole.get<std::string>());
    }
    return {std::move(course), ""};
}

parsed<std::vector<course_definition>> parse_courses(const std::map<std::string, std::string>& manifests) {
    std::vector<std::pair<std::string, course_definition>> by_folder;
    std::map<std::string, std::string> manifest_of_id;
    for (const auto& [path, text] : manifests) {
        const std::optional<std::string> folder = course_folder_of(path);
        if (!folder) {
            return {std::nullopt, path + ": not a course manifest (" + courses_path + "/<course>/" + course_file_name + ")"};
        }
        parsed<course_definition> course = parse_course_from_text(text);
        if (!course.value) {
            return {std::nullopt, path + ": " + course.error};
        }
        const auto [same, added] = manifest_of_id.emplace(course.value->id, path);
        if (!added) {
            return {std::nullopt, path + ": id \"" + course.value->id + "\" is also " + same->second + "'s"};
        }
        by_folder.emplace_back(*folder, course_in_folder(std::move(*course.value), *folder));
    }
    if (by_folder.empty()) {
        return {std::nullopt, std::string("no course in ") + courses_path};
    }
    std::sort(by_folder.begin(), by_folder.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<course_definition> courses;
    for (auto& [folder, course] : by_folder) {
        courses.push_back(std::move(course));
    }
    return {std::move(courses), ""};
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
