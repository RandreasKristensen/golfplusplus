#include "doctest.h"

#include "core/sky_frame.h"
#include "core/vcr_frame.h"
#include "game/json_util.h"
#include "game/lighting_loader.h"
#include "game/mode_dispatch.h"
#include "game/sky_clock.h"
#include "game/sun_lighting.h"
#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <utility>
#include <string>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace {
// Local solar noon on a date at a course.
double local_noon(const int year, const int month, const int day, const double longitude) {
    return game_day_at_local_time(static_cast<double>(days_from_civil(year, month, day)), longitude / 15.0, 12.0);
}

float elevation_degrees(const sky_state& sky) {
    return glm::degrees(sky.sun_elevation);
}

std::string fixture_lighting_text() {
    return *read_text_file(fixture_root() + "/" + lighting_path);
}
}

TEST_CASE("dates count days from 1970") {
    CHECK(days_from_civil(1970, 1, 1) == 0);
    CHECK(days_from_civil(2000, 3, 1) == 11017);
    CHECK(days_from_civil(1989, 6, 21) == 7111);
    CHECK(days_from_civil(1969, 12, 31) == -1);
    CHECK(parse_civil_date("1989-06-21") == std::optional<std::int64_t>(7111));
    for (const char* bad : {"1989-6-21", "1989-13-01", "1989-06-32", "1989/06/21", "89-06-21", "1989-06-21T"}) {
        CHECK(!parse_civil_date(bad).has_value());
    }
}

TEST_CASE("the sun stands as high at solar noon as the latitude and season say") {
    // Equinox: 90 degrees less the latitude.
    CHECK(std::abs(elevation_degrees(compute_sky_state(local_noon(2024, 3, 20, 10.2), 56.2, 10.2)) - 33.8f) < 1.0f);
    CHECK(std::abs(elevation_degrees(compute_sky_state(local_noon(2024, 3, 20, 0.0), 0.0, 0.0)) - 90.0f) < 1.0f);
    // Solstices in Denmark: high in June, low in December.
    CHECK(std::abs(elevation_degrees(compute_sky_state(local_noon(2024, 6, 20, 10.2), 56.2, 10.2)) - 57.2f) < 1.0f);
    CHECK(std::abs(elevation_degrees(compute_sky_state(local_noon(2024, 12, 21, 10.2), 56.2, 10.2)) - 10.4f) < 1.0f);
    // Southern hemisphere in June: low, and to the north.
    const sky_state south = compute_sky_state(local_noon(2024, 6, 20, 18.4), -33.9, 18.4);
    CHECK(std::abs(elevation_degrees(south) - 32.7f) < 1.0f);
    CHECK(south.sun_direction.z < 0.0f);
    // In Denmark the noon sun is due south (+z).
    const sky_state north = compute_sky_state(local_noon(2024, 6, 20, 10.2), 56.2, 10.2);
    CHECK(north.sun_direction.z > 0.0f);
    CHECK(std::abs(north.sun_direction.x) < 0.01f);
    CHECK(std::abs(north.day_fraction - 0.5f) < 0.0001f);
}

TEST_CASE("the sun rises in the east and sets in the west at the equinox") {
    const double date = static_cast<double>(days_from_civil(2024, 3, 20));
    const sky_state rise = compute_sky_state(game_day_at_local_time(date, 10.2 / 15.0, 6.0), 56.2, 10.2);
    CHECK(std::abs(elevation_degrees(rise)) < 1.5f);
    CHECK(rise.sun_direction.x > 0.98f);
    const sky_state set = compute_sky_state(game_day_at_local_time(date, 10.2 / 15.0, 18.0), 56.2, 10.2);
    CHECK(set.sun_direction.x < -0.98f);
    const sky_state midnight = compute_sky_state(game_day_at_local_time(date, 10.2 / 15.0, 0.0), 56.2, 10.2);
    CHECK(elevation_degrees(midnight) < -30.0f);
    CHECK(std::abs(glm::length(midnight.sun_direction) - 1.0f) < 0.0001f);
}

TEST_CASE("the same game day and course always give the same sky") {
    const double day = 7111.3721;
    const sky_state a = compute_sky_state(day, 56.18, 10.16);
    const sky_state b = compute_sky_state(day, 56.18, 10.16);
    CHECK(a.sun_direction == b.sun_direction);
    CHECK(a.moon_direction == b.moon_direction);
    CHECK(a.sun_elevation == b.sun_elevation);
    CHECK(a.moon_phase == b.moon_phase);
    CHECK(a.day_fraction == b.day_fraction);
}

TEST_CASE("the full moon stands opposite the sun, the new moon with it") {
    // 2000-01-21 04:40 UTC was full, 2000-02-05 13:03 UTC new.
    const sky_state full = compute_sky_state(static_cast<double>(days_from_civil(2000, 1, 21)) + 0.19, 56.2, 10.2);
    CHECK(full.moon_phase > 0.99f);
    CHECK(glm::dot(full.moon_direction, full.sun_direction) < -0.99f);
    const sky_state fresh = compute_sky_state(static_cast<double>(days_from_civil(2000, 2, 5)) + 0.54, 56.2, 10.2);
    CHECK(fresh.moon_phase < 0.01f);
    CHECK(glm::dot(fresh.moon_direction, fresh.sun_direction) > 0.99f);
}

TEST_CASE("the game day runs from the timeline's anchor at its rate") {
    sky_timeline timeline;
    timeline.anchor_unix_ms = days_from_civil(2026, 10, 1) * 86400000LL;
    timeline.anchor_game_day = 7111.25;
    timeline.game_days_per_real_day = 30.0;
    CHECK(game_day_at(timeline, timeline.anchor_unix_ms) == 7111.25);
    CHECK(std::abs(game_day_at(timeline, timeline.anchor_unix_ms + 48LL * 60 * 1000) - 7112.25) < 1e-9);

    sky_timeline frozen = timeline;
    frozen.game_days_per_real_day = 0.0;
    CHECK(game_day_at(frozen, timeline.anchor_unix_ms + 1000000000LL) == 7111.25);

    // Re-anchored at `now` with a new rate, the game day carries on from where it was.
    const std::int64_t now = timeline.anchor_unix_ms + 123456789LL;
    sky_timeline slower;
    slower.anchor_unix_ms = now;
    slower.anchor_game_day = game_day_at(timeline, now);
    slower.game_days_per_real_day = 10.0;
    CHECK(game_day_at(slower, now) == game_day_at(timeline, now));

    // A millisecond still counts in the year 2100.
    const std::int64_t far = days_from_civil(2100, 1, 1) * 86400000LL;
    const double step = game_day_at(timeline, far + 1) - game_day_at(timeline, far);
    CHECK(std::abs(step - 30.0 / 86400000.0) < 1e-3 * 30.0 / 86400000.0);
}

namespace {
// Danish summer time.
constexpr float summer_time_hours = 2.0f;

// The hour on a clock `utc_offset_hours` ahead of UTC at `game_day`.
double clock_hour(const double game_day, const double utc_offset_hours) {
    const double local = game_day + utc_offset_hours / 24.0;
    return (local - std::floor(local)) * 24.0;
}
}

TEST_CASE("the time of day runs at the timeline's rate on the course's date, which never changes") {
    sky_timeline timeline;
    timeline.anchor_unix_ms = days_from_civil(2026, 10, 1) * 86400000LL;
    timeline.anchor_game_day = 0.25;
    timeline.game_days_per_real_day = 30.0;
    timeline.year = 1989;
    const std::int64_t date = days_from_civil(1989, 6, 21);
    const text_assets& text = shipped_text_assets();
    const sky_controls running;
    for (const float offset : {-8.0f, 0.0f, summer_time_hours, 10.0f}) {
        const course_sky midsummer{6, 21, offset, -90.0f};
        CHECK(course_date(timeline, midsummer) == date);
        for (std::int64_t minute = 0; minute < 3LL * 24 * 60; minute += 7) {
            const std::int64_t now = timeline.anchor_unix_ms + minute * 60000LL + 11LL * 86400000LL;
            const double day = frame_game_day(timeline, midsummer, running, now);
            CHECK(make_datestamp(text, day, offset).date == "JUN 21 1989");
            CHECK(std::abs(std::remainder(clock_hour(day, offset) - clock_hour(game_day_at(timeline, now), offset), 24.0)) <
                  0.001);
        }
    }
    // Two real minutes are a game hour at 30 game days a real day.
    const course_sky utc{6, 21, 0.0f, -90.0f};
    const double start = frame_game_day(timeline, utc, running, timeline.anchor_unix_ms);
    const double later = frame_game_day(timeline, utc, running, timeline.anchor_unix_ms + 2LL * 60000);
    CHECK(std::abs((later - start) * 24.0 - 1.0) < 1e-6);
    CHECK(std::abs((start - static_cast<double>(date)) * 24.0 - 6.0) < 1e-6);
}

TEST_CASE("a pinned hour is on the course's clock and date, the scrub runs past it") {
    sky_timeline timeline;
    timeline.anchor_game_day = 0.25;
    timeline.game_days_per_real_day = 30.0;
    timeline.year = 1989;
    const course_sky midsummer{6, 21, summer_time_hours, -90.0f};
    sky_controls controls;
    controls.pinned_hour = 19.0f;
    const double pinned = frame_game_day(timeline, midsummer, controls, 999999999LL);
    CHECK(std::abs(clock_hour(pinned, summer_time_hours) - 19.0) < 1e-6);
    CHECK(std::floor(pinned + summer_time_hours / 24.0) == 7111.0);
    CHECK(frame_game_day(timeline, midsummer, controls, 0) == pinned);

    const sky_controls scrubbed = scrub_sky(controls, true, 1.0f);
    CHECK(frame_game_day(timeline, midsummer, scrubbed, 0) > pinned);
    CHECK(frame_game_day(timeline, midsummer, scrub_sky(scrubbed, true, 1.0f), 0) > frame_game_day(timeline, midsummer, scrubbed, 0));
    CHECK(scrub_sky(controls, false, 3.0f).scrub_game_days == 0.0);
    // Scrubbed past midnight, it is the same date's early morning.
    const double wrapped = frame_game_day(timeline, midsummer, scrub_sky(controls, true, 3.0f), 0);
    CHECK(std::floor(wrapped + summer_time_hours / 24.0) == 7111.0);
    CHECK(std::abs(clock_hour(wrapped, summer_time_hours) - 1.0) < 1e-6);
}

TEST_CASE("a pinned date takes the pinned hour off the course's date") {
    sky_timeline timeline;
    timeline.year = 1989;
    sky_controls controls;
    controls.pinned_hour = 23.0f;
    controls.pinned_date = days_from_civil(1989, 12, 21);
    for (const float offset : {-8.0f, 1.0f, 10.0f}) {
        const double day = frame_game_day(timeline, course_sky{6, 21, offset, -90.0f}, controls, 0);
        CHECK(std::floor(day + offset / 24.0) == static_cast<double>(*controls.pinned_date));
        CHECK(std::abs(clock_hour(day, offset) - 23.0) < 1e-6);
    }
    // A winter night in Denmark is truly dark, which its June never is.
    const double winter = frame_game_day(timeline, course_sky{6, 21, 1.0f, -90.0f}, controls, 0);
    CHECK(elevation_degrees(compute_sky_state(winter, 56.2, 10.2)) < -18.0f);
}

TEST_CASE("a Danish midsummer course is light from before four to eleven at night, with no true night") {
    sky_timeline timeline;
    timeline.year = 1989;
    const auto elevation_at = [&timeline](const course_sky& sky, const double hour) {
        sky_controls controls;
        controls.pinned_hour = static_cast<float>(hour);
        return elevation_degrees(compute_sky_state(frame_game_day(timeline, sky, controls, 0), 56.18, 10.16));
    };
    const course_sky midsummer{6, 21, summer_time_hours, -90.0f};
    // Light, the sun above -6 degrees, from before four to eleven on its clock.
    for (double hour = 3.75; hour <= 23.0; hour += 0.25) {
        CHECK(elevation_at(midsummer, hour) > -6.0f);
    }
    // Up before five, down by a quarter past ten.
    CHECK(elevation_at(midsummer, 5.0) > 0.0f);
    CHECK(elevation_at(midsummer, 22.25) < 0.0f);
    // The night is a blue dusk, never darker than -12 degrees.
    CHECK(elevation_at(midsummer, 1.0) < -6.0f);
    for (double hour = 0.0; hour <= 24.0; hour += 0.25) {
        CHECK(elevation_at(midsummer, hour) > -12.0f);
    }
    // A December course on the same land has a long dark night.
    CHECK(elevation_at(course_sky{12, 21, 1.0f, -90.0f}, 19.0) < -18.0f);
}

TEST_CASE("the lowest sun keeps a course's sun at or above its floor, still circling the sky") {
    sky_timeline timeline;
    timeline.year = 1989;
    const course_sky white_nights{6, 21, summer_time_hours, 2.0f};
    const auto sky_at = [&timeline, &white_nights](const double hour) {
        sky_controls controls;
        controls.pinned_hour = static_cast<float>(hour);
        return compute_sky_state(frame_game_day(timeline, white_nights, controls, 0), 56.18, 10.16);
    };
    for (double hour = 0.0; hour < 24.0; hour += 0.25) {
        const sky_state real = sky_at(hour);
        const sky_state floored = with_lowest_sun(real, white_nights.lowest_sun_deg);
        CHECK(elevation_degrees(floored) >= 2.0f - 0.001f);
        CHECK(near(glm::degrees(std::asin(floored.sun_direction.y)), elevation_degrees(floored), 0.01f));
        CHECK(std::abs(glm::length(floored.sun_direction) - 1.0f) < 0.0001f);
        CHECK(floored.moon_direction == real.moon_direction);
        if (elevation_degrees(real) >= 2.0f) {
            CHECK(floored.sun_direction == real.sun_direction);
        } else {
            // Raised along its own bearing.
            const float cross = floored.sun_direction.x * real.sun_direction.z - floored.sun_direction.z * real.sun_direction.x;
            CHECK(std::abs(cross) < 0.0001f);
            CHECK(floored.sun_direction.x * real.sun_direction.x + floored.sun_direction.z * real.sun_direction.z > 0.0f);
        }
    }
    // Held up through the night, it still goes from north-west to north-east.
    const sky_state midnight = with_lowest_sun(sky_at(0.0), white_nights.lowest_sun_deg);
    const sky_state dawn = with_lowest_sun(sky_at(4.0), white_nights.lowest_sun_deg);
    CHECK(elevation_degrees(sky_at(4.0)) < 2.0f);
    CHECK(midnight.sun_direction.x < 0.0f);
    CHECK(midnight.sun_direction.z < 0.0f);
    CHECK(dawn.sun_direction.x > 0.0f);
    CHECK(dawn.sun_direction.z < 0.0f);
    // Straight below, it rises due south.
    sky_state below;
    below.sun_direction = glm::vec3(0.0f, -1.0f, 0.0f);
    below.sun_elevation = -glm::half_pi<float>();
    CHECK(near(with_lowest_sun(below, 0.0f).sun_direction, glm::vec3(0.0f, 0.0f, 1.0f), 0.0001f));
}

TEST_CASE("online the sky follows the server's clock, offline the local one") {
    game_state state = started_game(fixture_hub_course());
    state.online.server_now = 2000000000000000LL;
    CHECK(shared_unix_ms(state, 5) == 5);
    state.play = play_mode::online;
    CHECK(shared_unix_ms(state, 5) == 2000000000000LL);
    state.online.server_now = 0;
    CHECK(shared_unix_ms(state, 5) == 5);

    // The same UTC gives the same sky either way.
    const lighting_palette palette = *parse_lighting_from_text(fixture_lighting_text());
    const std::int64_t now = days_from_civil(2026, 11, 3) * 86400000LL + 12345678LL;
    const game_state offline = started_game(fixture_hub_course());
    game_state online = offline;
    online.play = play_mode::online;
    online.online.server_now = now * 1000;
    const render_lighting offline_light = make_render_lighting(offline, palette, sky_controls{}, now);
    const render_lighting online_light = make_render_lighting(online, palette, sky_controls{}, now + 987654LL);
    CHECK(offline_light.sun_direction == online_light.sun_direction);
    CHECK(offline_light.ambient_sky == online_light.ambient_sky);
}

TEST_CASE("the frame's light comes from the course's sky and the palette") {
    const lighting_palette palette = *parse_lighting_from_text(fixture_lighting_text());
    game_state state = started_game(fixture_hub_course());
    sky_controls controls;
    controls.pinned_hour = 12.0f;
    const render_lighting noon = make_render_lighting(state, palette, controls, 0);
    CHECK(noon.sun_direction.y > 0.8f);
    CHECK(noon.sun_direction.z > 0.0f);
    CHECK(noon.sky_zenith == palette.keys.back().sky_zenith);
    CHECK(noon.haze_tint == palette.keys.back().haze_tint);
    controls.pinned_hour = 0.0f;
    const render_lighting midnight = make_render_lighting(state, palette, controls, 0);
    CHECK(midnight.sun_direction.y < 0.0f);
    const lighting_key light = lighting_at(palette, glm::degrees(std::asin(midnight.sun_direction.y)));
    CHECK(midnight.ambient_sky == light.ambient_sky);
    CHECK(midnight.ambient_ground == light.ambient_ground);
    CHECK(midnight.sky_blend == light.sky_blend);
    CHECK(midnight.ambient_sky != noon.ambient_sky);
    CHECK(midnight.horizon_glow == light.horizon_glow);
    CHECK(midnight.horizon_glow_strength == light.horizon_glow_strength);
    CHECK(noon.star_visibility == 0.0f);
    CHECK(midnight.star_visibility == star_visibility(palette.sky, glm::degrees(std::asin(midnight.sun_direction.y))));
    const sky_state sky = compute_sky_state(
        frame_game_day(state.tuning.sky_clock, state.course.sky, controls, 0),
        state.hub->world.latitude_deg, state.hub->world.longitude_deg);
    CHECK(near(midnight.moon_direction, sky.moon_direction, 0.0001f));
    CHECK(midnight.moon_phase == sky.moon_phase);

    // A course with a floor under its sun lights its midnight at the floor.
    state.course.sky.lowest_sun_deg = 3.0f;
    const render_lighting white_night = make_render_lighting(state, palette, controls, 0);
    CHECK(near(glm::degrees(std::asin(white_night.sun_direction.y)), 3.0f, 0.01f));
    CHECK(white_night.ambient_sky == lighting_at(palette, glm::degrees(std::asin(white_night.sun_direction.y))).ambient_sky);
    CHECK(white_night.star_visibility == 0.0f);
}

TEST_CASE("the stars fade in as the sun sinks past the palette's elevations") {
    const lighting_palette palette = *parse_lighting_from_text(fixture_lighting_text());
    const sky_style& sky = palette.sky;
    CHECK(star_visibility(sky, 30.0f) == 0.0f);
    CHECK(star_visibility(sky, sky.stars_appear_deg) == 0.0f);
    CHECK(star_visibility(sky, sky.stars_full_deg) == 1.0f);
    CHECK(star_visibility(sky, -90.0f) == 1.0f);
    const float middle = star_visibility(sky, (sky.stars_appear_deg + sky.stars_full_deg) * 0.5f);
    CHECK(near(middle, 0.5f, 0.0001f));
    float previous = 0.0f;
    for (float elevation = 0.0f; elevation >= -20.0f; elevation -= 0.25f) {
        const float shown = star_visibility(sky, elevation);
        CHECK(shown >= previous);
        previous = shown;
    }
}

TEST_CASE("the lighting palette needs every field, keys rising within -90 to 90") {
    const std::string text = fixture_lighting_text();
    const std::optional<lighting_palette> palette = parse_lighting_from_text(text);
    REQUIRE(palette.has_value());
    REQUIRE(!palette->keys.empty());
    for (std::size_t i = 1; i < palette->keys.size(); ++i) {
        CHECK(palette->keys[i].sun_elevation_deg > palette->keys[i - 1].sun_elevation_deg);
    }
    CHECK(palette->colors.figures.shirts.size() == 8U);
    CHECK(!parse_lighting_from_text("not json"));

    const json shipped = *parse_json(text);
    const auto parses = [](const json& value) { return parse_lighting_from_text(value.dump()).has_value(); };
    for (const auto& [section, fields] : shipped.items()) {
        if (section == "keys") {
            continue;
        }
        json without_section = shipped;
        without_section.erase(section);
        CHECK(!parses(without_section));
        for (const auto& [key, value] : fields.items()) {
            json missing = shipped;
            missing[section].erase(key);
            CHECK(!parses(missing));
        }
    }
    for (const auto& [key, value] : shipped["keys"][0].items()) {
        json missing = shipped;
        missing["keys"][0].erase(key);
        CHECK(!parses(missing));
    }

    json unsorted = shipped;
    std::swap(unsorted["keys"][0], unsorted["keys"][1]);
    CHECK(!parses(unsorted));
    json too_low = shipped;
    too_low["keys"][0]["sun_elevation_deg"] = -91;
    CHECK(!parses(too_low));
    json too_bright = shipped;
    too_bright["ground"]["rough"] = json::array({0.1, 1.2, 0.1});
    CHECK(!parses(too_bright));
    json no_keys = shipped;
    no_keys["keys"] = json::array();
    CHECK(!parses(no_keys));

    // The sky's angles stay within a quarter turn, its shares within 0..1,
    // and the stars come out as the sun goes down, not up.
    for (const auto& [key, value] : std::initializer_list<std::pair<const char*, double>>{
             {"sun_radius_deg", -1.0}, {"moon_radius_deg", 91.0}, {"star_density", 1.5}, {"star_twinkle", -0.1},
             {"stars_appear_deg", 5.0}, {"stars_full_deg", -91.0}}) {
        json bad = shipped;
        bad["sky"][key] = value;
        CHECK(!parses(bad));
    }
    json backwards = shipped;
    backwards["sky"]["stars_appear_deg"] = -12;
    backwards["sky"]["stars_full_deg"] = -6;
    CHECK(!parses(backwards));
    json glaring = shipped;
    glaring["keys"][0]["horizon_glow_strength"] = 1.5;
    CHECK(!parses(glaring));

    // The clouds catch the sun within a half turn of it, never from nowhere.
    for (const auto& [key, value] : std::initializer_list<std::pair<const char*, double>>{
             {"cloud_threshold", -0.1}, {"cloud_softness", 0.0}, {"cloud_sun_gain", 5.0}, {"cloud_sun_width_deg", 181.0}}) {
        json bad = shipped;
        bad["sky"][key] = value;
        CHECK(!parses(bad));
    }
    json bright_shade = shipped;
    bright_shade["sky"]["cloud_shade"] = json::array({0.5, 1.5, 0.5});
    CHECK(!parses(bright_shade));
    const sky_style& sky = palette->sky;
    CHECK(sky.cloud_softness > 0.0f);
    CHECK(sky.cloud_shade == glm::vec3(shipped["sky"]["cloud_shade"][0].get<float>(), shipped["sky"]["cloud_shade"][1].get<float>(),
                                       shipped["sky"]["cloud_shade"][2].get<float>()));
}

TEST_CASE("the palette covers every sun elevation and blends continuously between keys") {
    const lighting_palette palette = *parse_lighting_from_text(fixture_lighting_text());
    CHECK(lighting_at(palette, -90.0f).ambient_sky == palette.keys.front().ambient_sky);
    CHECK(lighting_at(palette, 90.0f).haze_tint == palette.keys.back().haze_tint);

    const lighting_key& low = palette.keys[1];
    const lighting_key& high = palette.keys[2];
    const lighting_key middle = lighting_at(palette, (low.sun_elevation_deg + high.sun_elevation_deg) * 0.5f);
    CHECK(near(middle.sun_color, (low.sun_color + high.sun_color) * 0.5f, 0.0001f));
    CHECK(near(middle.ambient_ground, (low.ambient_ground + high.ambient_ground) * 0.5f, 0.0001f));
    CHECK(near(middle.sky_blend, (low.sky_blend + high.sky_blend) * 0.5f, 0.0001f));
    CHECK(near(lighting_at(palette, high.sun_elevation_deg).sun_color, high.sun_color, 0.0001f));

    // No jump anywhere: a hundredth of a degree moves every colour very little.
    float largest_step = 0.0f;
    lighting_key previous = lighting_at(palette, -90.0f);
    for (int step = 1; step <= 18000; ++step) {
        const lighting_key key = lighting_at(palette, -90.0f + static_cast<float>(step) * 0.01f);
        largest_step = std::max({largest_step, glm::length(key.sun_color - previous.sun_color),
                                 glm::length(key.ambient_sky - previous.ambient_sky),
                                 glm::length(key.ambient_ground - previous.ambient_ground),
                                 glm::length(key.sky_zenith - previous.sky_zenith),
                                 glm::length(key.haze_tint - previous.haze_tint), std::abs(key.sky_blend - previous.sky_blend)});
        previous = key;
    }
    CHECK(largest_step < 0.01f);
}

TEST_CASE("dates count from 1970 both ways") {
    for (const std::int64_t days : {-800000LL, -1LL, 0LL, 59LL, 7213LL, 7230LL, 11016LL, 2932896LL}) {
        const civil_date date = civil_from_days(days);
        CHECK(days_from_civil(date.year, date.month, date.day) == days);
    }
    const civil_date leap = civil_from_days(days_from_civil(2000, 2, 29));
    CHECK(leap.year == 2000);
    CHECK(leap.month == 2);
    CHECK(leap.day == 29);
}

TEST_CASE("the datestamp is the course's local solar date and time on a 12-hour clock") {
    const text_assets& text = shipped_text_assets();
    const double october_second = static_cast<double>(days_from_civil(1989, 10, 2));
    const render_datestamp evening = make_datestamp(text, october_second + (19.0 + 42.0 / 60.0) / 24.0, 0.0);
    CHECK(evening.time == "PM 7:42");
    CHECK(evening.date == "OCT 2 1989");
    CHECK(make_datestamp(text, october_second, 0.0).time == "AM 12:00");
    CHECK(make_datestamp(text, october_second + 0.5, 0.0).time == "PM 12:00");
    CHECK(make_datestamp(text, october_second + 9.0 / 1440.0, 0.0).time == "AM 12:09");
    // 23:30 UTC is already the next morning six hours ahead.
    const render_datestamp east = make_datestamp(text, october_second + 23.5 / 24.0, 6.0);
    CHECK(east.time == "AM 5:30");
    CHECK(east.date == "OCT 3 1989");
    // A pinned hour shows as that hour, not a minute short.
    sky_controls controls;
    controls.pinned_hour = 20.0f;
    sky_timeline timeline;
    timeline.year = 1989;
    const double day = frame_game_day(timeline, course_sky{6, 21, 2.0f, -90.0f}, controls, 0);
    CHECK(make_datestamp(text, day, 2.0).time == "PM 8:00");
    CHECK(make_datestamp(text, day, 2.0).date == "JUN 21 1989");
}

TEST_CASE("the VCR plays in the menus and pauses on a still frame, the datestamp follows its setting, and OFFLINE shows only offline") {
    const text_assets& text = shipped_text_assets();
    const game_state state = started_game(fixture_hub_course());
    const render_vcr menu = make_render_vcr(text, state, sky_controls{}, 0, vcr_signals{true, false, true});
    CHECK(menu.tag == vcr_tag::play);
    CHECK(!menu.still_frame);
    CHECK(!menu.show_datestamp);
    CHECK(!menu.show_offline);
    const render_vcr playing = make_render_vcr(text, state, sky_controls{}, 0, vcr_signals{false, false, true});
    CHECK(playing.tag == vcr_tag::none);
    CHECK(!playing.still_frame);
    CHECK(playing.show_datestamp);
    CHECK(!playing.datestamp.time.empty());
    CHECK(!playing.datestamp.date.empty());
    CHECK(playing.show_offline);
    const render_vcr paused = make_render_vcr(text, state, sky_controls{}, 0, vcr_signals{false, true, false});
    CHECK(paused.tag == vcr_tag::pause);
    CHECK(paused.still_frame);
    CHECK(!paused.show_datestamp);
    CHECK(paused.show_offline);
    game_state online = state;
    online.play = play_mode::online;
    CHECK(!make_render_vcr(text, online, sky_controls{}, 0, vcr_signals{false, false, true}).show_offline);
}

TEST_CASE("shadows fade in with the sun, the night lights come on without it, and the ball glows past white at night") {
    const lighting_palette palette = *parse_lighting_from_text(fixture_lighting_text());
    const shadow_style& shadows = palette.shadows;
    CHECK(sun_shadow_share(shadows, shadows.sun_fade_start_deg - 1.0f) == 0.0f);
    CHECK(sun_shadow_share(shadows, shadows.sun_fade_full_deg + 1.0f) == 1.0f);
    CHECK(near(sun_shadow_share(shadows, (shadows.sun_fade_start_deg + shadows.sun_fade_full_deg) * 0.5f), 0.5f, 0.0001f));
    const night_light_style& lights = palette.night_lights;
    CHECK(night_light_share(lights, lights.on_deg + 1.0f) == 0.0f);
    CHECK(night_light_share(lights, lights.full_deg - 1.0f) == 1.0f);
    float previous = 0.0f;
    for (float elevation = 20.0f; elevation >= -20.0f; elevation -= 0.25f) {
        const float on = night_light_share(lights, elevation);
        CHECK(on >= previous);
        previous = on;
    }

    const glow_floors& day = palette.colors.glow;
    CHECK(night_glow_floors(day, lights, 0.0f).ball == day.ball);
    const glow_floors night = night_glow_floors(day, lights, 1.0f);
    CHECK(night.ball == std::max(day.ball, lights.ball_glow));
    CHECK(night.ember == std::max(day.ember, lights.ember_glow));
    CHECK(night.flag == day.flag);

    const json shipped = *parse_json(fixture_lighting_text());
    const auto parses = [](const json& value) { return parse_lighting_from_text(value.dump()).has_value(); };
    json backwards_fade = shipped;
    backwards_fade["shadows"]["sun_fade_start_deg"] = shipped["shadows"]["sun_fade_full_deg"];
    CHECK(!parses(backwards_fade));
    json backwards_lights = shipped;
    backwards_lights["night_lights"]["full_deg"] = shipped["night_lights"]["on_deg"];
    CHECK(!parses(backwards_lights));
    json soft_past_edge = shipped;
    soft_past_edge["night_lights"]["headlight_soft_deg"] = shipped["night_lights"]["headlight_cone_deg"];
    CHECK(!parses(soft_past_edge));
    json flood_past_edge = shipped;
    flood_past_edge["night_lights"]["floodlight_soft_deg"] = shipped["night_lights"]["floodlight_cone_deg"];
    CHECK(!parses(flood_past_edge));
    json blinding = shipped;
    blinding["night_lights"]["floodlight"]["intensity"] = 17;
    CHECK(!parses(blinding));
    json unlit = shipped;
    unlit["night_lights"]["ball"].erase("color");
    CHECK(!parses(unlit));
    json stretched = shipped;
    stretched["shadows"]["max_stretch"] = 0.5;
    CHECK(!parses(stretched));

    // The trees' shadow map is a power of two texels square, over some ground.
    CHECK(shadows.map_resolution == shipped["shadows"]["map_resolution"].get<int>());
    for (const json& resolution : {json(1000), json(32), json(8192), json(1024.5), json("1024")}) {
        json bad = shipped;
        bad["shadows"]["map_resolution"] = resolution;
        CHECK(!parses(bad));
    }
    for (const auto& [key, value] : std::initializer_list<std::pair<const char*, double>>{
             {"map_extent", 5.0}, {"map_ahead", 0.6}, {"map_strength", 1.5}, {"map_softness", -1.0}, {"map_edge_fade", 0.0},
             {"tree_contact_share", 1.1}}) {
        json bad = shipped;
        bad["shadows"][key] = value;
        CHECK(!parses(bad));
    }
}
