#include "game/visual_tour_loader.h"

#include "game/content_files.h"
#include "game/json_util.h"
#include "game/sky_clock.h"

#include <filesystem>

namespace {
// `key` of `object` within min..max, both included.
std::optional<float> bounded(const json& object, const char* key, const float min, const float max) {
    const std::optional<float> value = json_float(object, key);
    return value && *value >= min && *value <= max ? value : std::nullopt;
}

std::optional<tour_flight> parse_flight(const json& object) {
    const std::optional<float> speed = bounded(object, "speed_meters_per_second", 0.1f, 100.0f);
    const std::optional<float> height = bounded(object, "height_meters", 0.0f, 500.0f);
    const std::optional<float> clearance = bounded(object, "tree_clearance_meters", 0.0f, 100.0f);
    const std::optional<float> look_ahead = bounded(object, "look_ahead_meters", 1.0f, 1000.0f);
    const std::optional<float> look_height = bounded(object, "look_height_meters", 0.0f, 500.0f);
    const std::optional<float> ease = bounded(object, "ease_meters", 0.0f, 1000.0f);
    if (!speed || !height || !clearance || !look_ahead || !look_height || !ease) {
        return std::nullopt;
    }
    return tour_flight{*speed, *height, *clearance, *look_ahead, *look_height, *ease};
}

std::optional<tour_stop> parse_stop(const json& object) {
    const std::optional<float> hour = bounded(object, "hour", 0.0f, 24.0f);
    const std::optional<std::string> weather_name = json_string(object, "weather");
    const std::optional<weather_kind> weather = weather_name ? weather_kind_from_name(*weather_name) : std::nullopt;
    const std::optional<float> wetness = bounded(object, "wetness", 0.0f, 1.0f);
    const std::optional<std::string> title = json_string(object, "title");
    if (!hour || !weather || !wetness || !title || title->empty()) {
        return std::nullopt;
    }
    return tour_stop{*hour, *weather, *wetness, *title};
}
}

std::optional<visual_tour> parse_visual_tour_from_text(const std::string& text) {
    const std::optional<json> root = parse_json(text);
    if (!root) {
        return std::nullopt;
    }
    const std::optional<std::string> date_text = json_string(*root, "date");
    const std::optional<std::int64_t> date = date_text ? parse_civil_date(*date_text) : std::nullopt;
    const std::optional<float> stop_seconds = bounded(*root, "stop_seconds", 1.0f, 3600.0f);
    const std::optional<float> title_seconds = bounded(*root, "title_seconds", 0.0f, 3600.0f);
    const std::optional<float> transition_seconds = bounded(*root, "transition_seconds", 0.0f, 3600.0f);
    const json* flight_object = json_object(*root, "flight");
    const std::optional<tour_flight> flight = flight_object != nullptr ? parse_flight(*flight_object) : std::nullopt;
    const json* stops = json_array(*root, "stops");
    if (!date || !stop_seconds || !title_seconds || !transition_seconds || !flight || stops == nullptr || stops->empty()) {
        return std::nullopt;
    }
    visual_tour tour;
    tour.date = *date;
    tour.stop_seconds = *stop_seconds;
    tour.title_seconds = *title_seconds;
    tour.transition_seconds = *transition_seconds;
    tour.flight = *flight;
    for (const json& entry : *stops) {
        const std::optional<tour_stop> stop = entry.is_object() ? parse_stop(entry) : std::nullopt;
        if (!stop) {
            return std::nullopt;
        }
        tour.stops.push_back(*stop);
    }
    return tour;
}

std::optional<visual_tour> load_visual_tour(const std::string& asset_root) {
    const std::optional<std::string> text = read_text_file(std::filesystem::path(asset_root) / visual_tour_path);
    return text ? parse_visual_tour_from_text(*text) : std::nullopt;
}
