#pragma once

// The visual tour from assets/picture/visual_tour.json: a camera flown along
// a course's holes through stops at times of day and weathers, to show
// what the picture does in each (core/visual_tour.h). Art direction, loaded
// by app like the picture, so not part of game_content.

#include "game/weather.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

inline constexpr const char* visual_tour_path = "picture/visual_tour.json";

// One stop: the sky at `hour` (0..24) on the course's clock, the weather
// held at `weather`'s preset on ground as wet as `wetness` (0..1), and the
// title (a text id) burned in as it starts.
struct tour_stop {
    float hour = 0.0f;
    weather_kind weather = weather_kind::clear;
    float wetness = 0.0f;
    std::string title;
};

// How the camera flies: along every hole's line of play at
// speed_meters_per_second, height_meters over the ground and
// tree_clearance_meters over any tree top under it, rising and falling over
// ease_meters, looking look_ahead_meters along the line at
// look_height_meters over the ground there.
struct tour_flight {
    float speed_meters_per_second = 0.0f;
    float height_meters = 0.0f;
    float tree_clearance_meters = 0.0f;
    float look_ahead_meters = 0.0f;
    float look_height_meters = 0.0f;
    float ease_meters = 0.0f;
};

// Every stop on `date` (days since 1970-01-01), stop_seconds each, its
// title shown for its first title_seconds, its hour reached from the last
// stop's over its first transition_seconds (its weather fades in over the
// palette's weather fade_seconds).
struct visual_tour {
    std::int64_t date = 0;
    float stop_seconds = 0.0f;
    float title_seconds = 0.0f;
    float transition_seconds = 0.0f;
    tour_flight flight;
    std::vector<tour_stop> stops;
};

// nullopt unless every field is there and in range, the date is YYYY-MM-DD,
// every weather a weather_kind's name, and there is at least one stop.
std::optional<visual_tour> parse_visual_tour_from_text(const std::string& text);
std::optional<visual_tour> load_visual_tour(const std::string& asset_root);
