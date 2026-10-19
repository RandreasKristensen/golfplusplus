#pragma once

// The sky's clock: the tuning's sky_timeline (game_tuning.h) at UTC now
// gives the time of day, on the course's own date (course_sky), which never
// changes; turned into where the sun and moon stand over the course. Pure,
// and visual only: nothing in play reads it. A gameplay rule that ever
// depends on the time of day uses game_day_at (plain arithmetic the server
// can replay), never the sun's trigonometry.

#include "game/course_definition.h"
#include "game/game_tuning.h"

#include <cstdint>
#include <optional>
#include <string>

#include <glm/vec3.hpp>

inline constexpr double milliseconds_per_day = 86400000.0;

// Days since 1970-01-01 of a date in the proleptic Gregorian calendar
// (month 1..12, day 1..31).
constexpr std::int64_t days_from_civil(const int year, const int month, const int day) {
    const std::int64_t y = static_cast<std::int64_t>(year) - (month <= 2 ? 1 : 0);
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const std::int64_t year_of_era = y - era * 400;
    const std::int64_t day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const std::int64_t day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + day_of_era - 719468;
}

struct civil_date {
    int year = 1970;
    int month = 1;  // 1..12
    int day = 1;    // 1..31
};

// The date `days` after 1970-01-01, the inverse of days_from_civil.
constexpr civil_date civil_from_days(const std::int64_t days) {
    const std::int64_t z = days + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const std::int64_t day_of_era = z - era * 146097;
    const std::int64_t year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const std::int64_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const std::int64_t shifted_month = (5 * day_of_year + 2) / 153;
    const int day = static_cast<int>(day_of_year - (153 * shifted_month + 2) / 5 + 1);
    const int month = static_cast<int>(shifted_month < 10 ? shifted_month + 3 : shifted_month - 9);
    const int year = static_cast<int>(year_of_era + era * 400 + (month <= 2 ? 1 : 0));
    return civil_date{year, month, day};
}

// Days since 1970-01-01 of a "YYYY-MM-DD" date; nullopt unless it is one.
std::optional<std::int64_t> parse_civil_date(const std::string& text);

// A "MM-DD" day of the year (year 1970's: February has 28 days); nullopt
// unless it is one.
std::optional<civil_date> parse_month_day(const std::string& text);

// The game's day count at `now_unix_ms` (real UTC milliseconds): its fraction
// is the time of day in UTC on the game's clock, its whole part counts game
// days but is no date.
double game_day_at(const sky_timeline& timeline, std::int64_t now_unix_ms);

// The course's date: its day of the year in the timeline's year.
std::int64_t course_date(const sky_timeline& timeline, const course_sky& sky);

// The moment whose local time `utc_offset_hours` ahead of UTC is
// `game_day`'s, on the local `date` (days since 1970-01-01).
double game_day_on_date(double game_day, std::int64_t date, double utc_offset_hours);

// The moment on `game_day`'s local date, `utc_offset_hours` ahead of UTC, at
// which the local time is `local_hour` (0..24).
double game_day_at_local_time(double game_day, double utc_offset_hours, double local_hour);

struct sky_state {
    // World space (x east, y up, z south), unit length, towards each.
    glm::vec3 sun_direction{0.0f, 1.0f, 0.0f};
    glm::vec3 moon_direction{0.0f, -1.0f, 0.0f};
    float sun_elevation = 0.0f;  // radians above the horizon
    float moon_phase = 0.0f;     // how much of it is lit: 0 new .. 1 full
    float day_fraction = 0.0f;   // local solar time, 0 at midnight .. 1
};

// The sky over a course at `latitude_deg` north, `longitude_deg` east. A
// low-precision solar position (declination and hour angle, no equation of
// time); the moon is the anti-sun offset by its phase.
sky_state compute_sky_state(double game_day, double latitude_deg, double longitude_deg);

// `sky` with its sun raised along its own bearing to `lowest_sun_deg` when it
// is lower, so it keeps circling the horizon through the night.
sky_state with_lowest_sun(sky_state sky, float lowest_sun_deg);
