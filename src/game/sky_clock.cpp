#include "game/sky_clock.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

namespace {
constexpr double obliquity_deg = 23.44;
constexpr double tropical_year_days = 365.2422;
constexpr double synodic_month_days = 29.530589;
// 2000-12-21 13:37 UTC and 2000-01-06 18:14 UTC.
constexpr double december_solstice_day = static_cast<double>(days_from_civil(2000, 12, 21)) + 0.5674;
constexpr double new_moon_day = static_cast<double>(days_from_civil(2000, 1, 6)) + 0.7597;

double fraction(const double value) {
    return value - std::floor(value);
}

// Towards a body at `declination` and `hour_angle` (radians, 0 when due
// south at its highest), seen from `latitude`: world x east, y up, z south.
glm::vec3 sky_direction(const double declination, const double hour_angle, const double latitude) {
    const double east = -std::cos(declination) * std::sin(hour_angle);
    const double north =
        std::cos(latitude) * std::sin(declination) - std::sin(latitude) * std::cos(declination) * std::cos(hour_angle);
    const double up =
        std::sin(latitude) * std::sin(declination) + std::cos(latitude) * std::cos(declination) * std::cos(hour_angle);
    return glm::vec3(static_cast<float>(east), static_cast<float>(up), static_cast<float>(-north));
}

// The number made of `count` digits from `text[first]`.
std::optional<int> digits(const std::string& text, const std::size_t first, const std::size_t count) {
    int value = 0;
    for (std::size_t i = first; i < first + count; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return std::nullopt;
        }
        value = value * 10 + (text[i] - '0');
    }
    return value;
}
}

std::optional<std::int64_t> parse_civil_date(const std::string& text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        return std::nullopt;
    }
    const std::optional<int> year = digits(text, 0, 4);
    const std::optional<int> month = digits(text, 5, 2);
    const std::optional<int> day = digits(text, 8, 2);
    if (!year || !month || !day || *month < 1 || *month > 12 || *day < 1 || *day > 31) {
        return std::nullopt;
    }
    return days_from_civil(*year, *month, *day);
}

std::optional<civil_date> parse_month_day(const std::string& text) {
    constexpr int days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (text.size() != 5 || text[2] != '-') {
        return std::nullopt;
    }
    const std::optional<int> month = digits(text, 0, 2);
    const std::optional<int> day = digits(text, 3, 2);
    if (!month || !day || *month < 1 || *month > 12 || *day < 1 || *day > days_in_month[*month - 1]) {
        return std::nullopt;
    }
    return civil_date{1970, *month, *day};
}

double game_day_at(const sky_timeline& timeline, const std::int64_t now_unix_ms) {
    const double real_days = static_cast<double>(now_unix_ms - timeline.anchor_unix_ms) / milliseconds_per_day;
    return timeline.anchor_game_day + real_days * timeline.game_days_per_real_day;
}

std::int64_t course_date(const sky_timeline& timeline, const course_sky& sky) {
    return days_from_civil(timeline.year, sky.month, sky.day);
}

double game_day_on_date(const double game_day, const std::int64_t date, const double utc_offset_hours) {
    const double offset = utc_offset_hours / 24.0;
    const double time_of_day = fraction(game_day + offset);
    // Within half a second of midnight is the date's start, so a clock read
    // to the second never shows the next date.
    const double wrapped = time_of_day > 1.0 - 0.5 / 86400.0 ? 0.0 : time_of_day;
    return static_cast<double>(date) + wrapped - offset;
}

double game_day_at_local_time(const double game_day, const double utc_offset_hours, const double local_hour) {
    const double offset = utc_offset_hours / 24.0;
    return std::floor(game_day + offset) + local_hour / 24.0 - offset;
}

sky_state compute_sky_state(const double game_day, const double latitude_deg, const double longitude_deg) {
    const double two_pi = glm::two_pi<double>();
    const double latitude = glm::radians(latitude_deg);
    const double day_fraction = fraction(game_day + longitude_deg / 360.0);
    const double hour_angle = (day_fraction - 0.5) * two_pi;
    const double season = two_pi * (game_day - december_solstice_day) / tropical_year_days;
    const double declination = -glm::radians(obliquity_deg) * std::cos(season);
    const double moon_age = fraction((game_day - new_moon_day) / synodic_month_days);

    sky_state sky;
    sky.sun_direction = sky_direction(declination, hour_angle, latitude);
    sky.moon_direction = sky_direction(declination * std::cos(moon_age * two_pi), hour_angle - moon_age * two_pi, latitude);
    sky.sun_elevation = std::asin(std::clamp(sky.sun_direction.y, -1.0f, 1.0f));
    sky.moon_phase = static_cast<float>((1.0 - std::cos(moon_age * two_pi)) * 0.5);
    sky.day_fraction = static_cast<float>(day_fraction);
    return sky;
}

sky_state with_lowest_sun(sky_state sky, const float lowest_sun_deg) {
    const float lowest = glm::radians(lowest_sun_deg);
    if (sky.sun_elevation >= lowest) {
        return sky;
    }
    const float across = std::hypot(sky.sun_direction.x, sky.sun_direction.z);
    // Straight below, the sun has no bearing; it rises due south.
    const float east = across > 1e-6f ? sky.sun_direction.x / across : 0.0f;
    const float south = across > 1e-6f ? sky.sun_direction.z / across : 1.0f;
    sky.sun_direction = glm::vec3(east * std::cos(lowest), std::sin(lowest), south * std::cos(lowest));
    sky.sun_elevation = lowest;
    return sky;
}
