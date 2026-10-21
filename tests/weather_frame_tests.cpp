#include "doctest.h"

#include "core/weather_frame.h"
#include "game/content_files.h"
#include "game/json_util.h"
#include "game/sun_lighting.h"
#include "game/weather_lighting.h"
#include "renderer/scene_haze.h"
#include "test_support.h"

#include <cmath>
#include <initializer_list>
#include <string>
#include <tuple>
#include <utility>
#include <algorithm>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

namespace {
const lighting_palette& fixture_palette() {
    static const lighting_palette palette = *load_lighting(fixture_root());
    return palette;
}

const game_tuning& fixture_tuning() {
    static const game_tuning tuning = fixture_content().tuning;
    return tuning;
}

const wind_profile& fixture_wind() {
    return fixture_tuning().wind.profile;
}

weather_state weather_of(const weather_kind kind, const glm::vec3& wind, const float wetness) {
    weather_state weather = with_weather_kind(weather_state{}, kind, fixture_tuning().weather);
    weather.wind = wind;
    weather.wetness = wetness;
    return weather;
}

weather_presentation settled(const weather_state& weather) {
    return update_weather_presentation(weather_presentation{}, weather, fixture_palette(), fixture_wind(), 2.0f, 0.0f, true);
}

// Where weather_streak.vert draws a streak seeded `seed` (each 0..1).
glm::vec3 streak_head(const glm::vec3& seed, const render_streaks& streaks) {
    const glm::vec3 local = glm::fract(seed + (streaks.offset - streaks.center) / streaks.size) - 0.5f;
    return streaks.center + local * streaks.size;
}
}

TEST_CASE("the weather as drawn starts as it is, then fades towards a change over the palette's fade") {
    const weather_state clear = weather_of(weather_kind::clear, glm::vec3(0.0f), 0.0f);
    const weather_state heavy = weather_of(weather_kind::heavy_rain, glm::vec3(0.0f), 1.0f);
    const weather_presentation start = settled(heavy);
    CHECK(start.started);
    CHECK(start.shown.cloud == heavy.cloud);
    CHECK(start.shown.rain == heavy.rain);
    CHECK(start.shown.wetness == 1.0f);
    CHECK(start.shown.rainbow == 0.0f);

    const float fade = fixture_palette().weather.fade_seconds;
    REQUIRE(fade > 0.0f);
    weather_presentation easing = start;
    easing = update_weather_presentation(easing, clear, fixture_palette(), fixture_wind(), 2.0f, fade, true);
    CHECK(near(easing.shown.rain, heavy.rain * std::exp(-1.0f), 0.001f));
    float previous = easing.shown.cloud;
    for (int frame = 0; frame < 600; ++frame) {
        easing = update_weather_presentation(easing, clear, fixture_palette(), fixture_wind(), 2.0f, 1.0f / 60.0f, true);
        CHECK(easing.shown.cloud <= previous);
        CHECK(easing.shown.cloud >= 0.0f);
        previous = easing.shown.cloud;
    }
    CHECK(easing.shown.cloud < 0.2f);

    // A sun shower's rainbow, and only a sun shower's.
    CHECK(shown_target(weather_of(weather_kind::sun_shower, glm::vec3(0.0f), 0.4f)).rainbow == 1.0f);
    for (const weather_kind kind : {weather_kind::clear, weather_kind::cloudy, weather_kind::light_rain, weather_kind::rain,
                                    weather_kind::heavy_rain}) {
        CHECK(shown_target(weather_of(kind, glm::vec3(0.0f), 0.0f)).rainbow == 0.0f);
    }
    CHECK(eased_toward(0.25f, 1.0f, 1.0f, 0.0f) == 1.0f);
    CHECK(eased_toward(0.25f, 1.0f, 0.0f, 3.0f) == 0.25f);
}

TEST_CASE("the three rains fall thicker and faster in turn, slanted by the wind; clear and cloudy skies are dry") {
    const glm::vec3 east(6.0f, 0.0f, 0.0f);
    const auto rain_of = [&](const weather_kind kind) {
        const weather_state weather = weather_of(kind, east, 0.0f);
        return make_render_weather(settled(weather), weather, fixture_palette(), fixture_wind(), 2.0f, glm::vec3(0.0f), 0.0f).rain;
    };
    CHECK(rain_of(weather_kind::clear).count == 0);
    CHECK(rain_of(weather_kind::cloudy).count == 0);
    const render_streaks light = rain_of(weather_kind::light_rain);
    const render_streaks rain = rain_of(weather_kind::rain);
    const render_streaks heavy = rain_of(weather_kind::heavy_rain);
    CHECK(light.count > 0);
    CHECK(rain.count > light.count);
    CHECK(heavy.count > rain.count);
    CHECK(heavy.count == fixture_palette().rain.max_drops);
    // Water, lit by the sky seen through it.
    CHECK(heavy.water);
    CHECK(rain_of(weather_kind::sun_shower).count == light.count);
    CHECK(heavy.velocity.y < rain.velocity.y);
    CHECK(rain.velocity.y < light.velocity.y);
    CHECK(light.velocity.y < 0.0f);
    CHECK(light.alpha < heavy.alpha);
    // Blown downwind, by less than the wind measured 10 m up.
    CHECK(heavy.velocity.x > 0.0f);
    CHECK(heavy.velocity.x < east.x);
    CHECK(heavy.velocity.z == 0.0f);
}

TEST_CASE("rain wraps round the camera: each drop stays put as the camera moves, always inside the box round it") {
    const weather_state weather = weather_of(weather_kind::rain, glm::vec3(3.0f, 0.0f, -2.0f), 0.0f);
    weather_presentation state = settled(weather);
    const glm::vec3 seed(0.3f, 0.7f, 0.1f);
    const glm::vec3 camera(250.0f, 12.0f, -480.0f);
    render_weather frame = make_render_weather(state, weather, fixture_palette(), fixture_wind(), 2.0f, camera, 10.0f);
    const glm::vec3 size = frame.rain.size;
    const glm::vec3 before = streak_head(seed, frame.rain);
    CHECK(glm::all(glm::lessThanEqual(glm::abs(before - camera), size * 0.5f + 0.0001f)));

    // A small step of the camera leaves the drop where it was...
    frame = make_render_weather(state, weather, fixture_palette(), fixture_wind(), 2.0f, camera + glm::vec3(0.2f, 0.0f, 0.1f), 10.0f);
    CHECK(near(streak_head(seed, frame.rain), before, 0.001f));
    // ...and a long walk finds it again within the box round the camera.
    const glm::vec3 far_camera = camera + glm::vec3(1000.0f, 3.0f, 777.0f);
    frame = make_render_weather(state, weather, fixture_palette(), fixture_wind(), 2.0f, far_camera, 10.0f);
    CHECK(glm::all(glm::lessThanEqual(glm::abs(streak_head(seed, frame.rain) - far_camera), size * 0.5f + 0.0001f)));

    // Time moves it along its velocity, the offset staying within the box.
    const float dt = 0.05f;
    state = update_weather_presentation(state, weather, fixture_palette(), fixture_wind(), 2.0f, dt, true);
    frame = make_render_weather(state, weather, fixture_palette(), fixture_wind(), 2.0f, camera, 10.0f);
    CHECK(near(streak_head(seed, frame.rain), before + frame.rain.velocity * dt, 0.001f));
    for (int frame_index = 0; frame_index < 2000; ++frame_index) {
        state = update_weather_presentation(state, weather, fixture_palette(), fixture_wind(), 2.0f, 1.0f / 30.0f, true);
        CHECK(glm::all(glm::greaterThanEqual(state.rain_offset, glm::vec3(0.0f))));
        CHECK(glm::all(glm::lessThan(state.rain_offset, size)));
    }
    CHECK(near(wrapped_in_volume(glm::vec3(-1.0f, 31.0f, 5.0f), glm::vec3(30.0f)), glm::vec3(29.0f, 1.0f, 5.0f), 0.0001f));
}

TEST_CASE("the wind shows: flags stream out and flap faster, and more specks blow past, the stronger it is") {
    const auto frame_in = [](const glm::vec3& wind) {
        const weather_state weather = weather_of(weather_kind::clear, wind, 0.0f);
        weather_presentation state = settled(weather);
        state = update_weather_presentation(state, weather, fixture_palette(), fixture_wind(), 2.0f, 0.1f, true);
        return std::make_pair(state, make_render_weather(state, weather, fixture_palette(), fixture_wind(), 2.0f,
                                                         glm::vec3(5.0f, 3.0f, 5.0f), 1.0f));
    };
    const auto [calm_state, calm] = frame_in(glm::vec3(0.0f, 0.0f, 0.3f));
    const auto [breeze_state, breeze] = frame_in(glm::vec3(0.0f, 0.0f, 5.0f));
    const auto [gale_state, gale] = frame_in(glm::vec3(0.0f, 0.0f, 25.0f));
    CHECK(calm.flag_lift == 0.0f);
    CHECK(calm.flag_flap == 0.0f);
    CHECK(breeze.flag_lift > 0.0f);
    CHECK(breeze.flag_lift < 1.0f);
    CHECK(gale.flag_lift == 1.0f);
    CHECK(gale.flag_wind.z > 0.0f);
    CHECK(gale.flag_wind.x == 0.0f);
    CHECK(calm_state.flag_phase < breeze_state.flag_phase);
    CHECK(breeze_state.flag_phase < gale_state.flag_phase);

    CHECK(calm.specks.count > 0);
    CHECK(calm.specks.count < breeze.specks.count);
    CHECK(gale.specks.count == fixture_palette().wind.max_specks);
    CHECK(gale.specks.velocity.z > breeze.specks.velocity.z);
    CHECK(gale.specks.velocity.y == 0.0f);
    CHECK(!gale.specks.water);
    // Low over the ground round the camera.
    CHECK(gale.specks.center == glm::vec3(5.0f, 1.0f + fixture_palette().wind.speck_height, 5.0f));
    CHECK(gale.specks.size.y == 2.0f * fixture_palette().wind.speck_height);
    CHECK(gale.rain.count == 0);
}

TEST_CASE("cloud cover dims the sun and its shadows and turns the light grey; rain darkens it further") {
    const lighting_palette& palette = fixture_palette();
    const weather_style& style = palette.weather;
    const lighting_key day = lighting_at(palette, 40.0f);
    const lighting_key same = weathered_lighting(day, style, shown_weather{});
    CHECK(same.sun_color == day.sun_color);
    CHECK(same.ambient_sky == day.ambient_sky);
    CHECK(same.sky_zenith == day.sky_zenith);
    CHECK(same.exposure == day.exposure);
    CHECK(weathered_sun_shadow_share(0.8f, style, shown_weather{}) == 0.8f);

    const lighting_key overcast = weathered_lighting(day, style, shown_weather{1.0f, 0.0f, 0.0f, 0.0f, 0.0f});
    const lighting_key pouring = weathered_lighting(day, style, shown_weather{1.0f, 1.0f, 0.0f, 0.0f, 0.0f});
    CHECK(glm::length(overcast.sun_color) < glm::length(day.sun_color) * 0.5f);
    CHECK(overcast.sky_blend == std::max(day.sky_blend, style.overcast_sky_blend));
    const auto spread = [](const glm::vec3& color) { return std::max({color.r, color.g, color.b}) - std::min({color.r, color.g, color.b}); };
    CHECK(spread(overcast.sky_zenith) < spread(day.sky_zenith));
    CHECK(spread(overcast.ambient_sky) <= spread(day.ambient_sky));
    CHECK(overcast.saturation < day.saturation);
    CHECK(glm::length(pouring.ambient_sky) < glm::length(overcast.ambient_sky));
    CHECK(glm::length(pouring.sky_zenith) < glm::length(overcast.sky_zenith));
    CHECK(pouring.gain_noise > overcast.gain_noise);
    CHECK(weathered_sun_shadow_share(1.0f, style, shown_weather{1.0f, 0.0f, 0.0f, 0.0f, 0.0f}) == style.overcast_shadows);

    // The overcast sky follows the time of day: dark at night.
    const lighting_key night = lighting_at(palette, -30.0f);
    CHECK(glm::length(weathered_lighting(night, style, shown_weather{1.0f, 0.0f, 0.0f, 0.0f, 0.0f}).sky_zenith) <
          glm::length(overcast.sky_zenith) * 0.5f);
}

TEST_CASE("weather thickens the haze and greys it; wet ground darkens and shines") {
    const weather_style& style = fixture_palette().weather;
    scene_haze haze;
    haze.color = glm::vec3(0.6f, 0.7f, 0.9f);
    haze.far_amount = 0.4f;
    haze.full_distance = 500.0f;
    const scene_haze clear = weathered_haze(haze, haze_under_weather(style, shown_weather{}));
    CHECK(clear.color == haze.color);
    CHECK(clear.far_amount == haze.far_amount);
    CHECK(clear.full_distance == haze.full_distance);
    const scene_haze cloudy = weathered_haze(haze, haze_under_weather(style, shown_weather{1.0f, 0.0f, 0.0f, 0.0f, 0.0f}));
    const scene_haze pouring = weathered_haze(haze, haze_under_weather(style, shown_weather{1.0f, 1.0f, 0.0f, 0.0f, 0.0f}));
    CHECK(cloudy.far_amount > haze.far_amount);
    CHECK(pouring.far_amount > cloudy.far_amount);
    CHECK(pouring.far_amount <= 1.0f);
    CHECK(pouring.full_distance < cloudy.full_distance);
    CHECK(cloudy.color.b - cloudy.color.r < haze.color.b - haze.color.r);

    const render_sky_weather dry = make_render_sky_weather(shown_weather{}, style);
    CHECK(dry.wet_albedo == 1.0f);
    CHECK(dry.wet_gloss == 0.0f);
    CHECK(dry.cover == 0.0f);
    const render_sky_weather soaked = make_render_sky_weather(shown_weather{0.0f, 0.0f, 0.0f, 1.0f, 1.0f}, style);
    CHECK(soaked.wet_albedo == style.wet_albedo);
    CHECK(soaked.wet_gloss == style.wet_gloss);
    CHECK(soaked.rainbow == 1.0f);
    // Rain's clouds are dark where the clear sky's are bright.
    CHECK(make_render_sky_weather(shown_weather{1.0f, 1.0f, 0.0f, 0.0f, 0.0f}, style).mottle < 0.0f);
    CHECK(make_render_sky_weather(shown_weather{1.0f, 0.0f, 0.0f, 0.0f, 0.0f}, style).mottle > 0.0f);
}

TEST_CASE("the weather's numbers stay in range: a rainbow inside its secondary, flags calm below full") {
    const std::string text = *read_text_file(fixture_root() + "/" + lighting_path);
    const json shipped = *parse_json(text);
    const auto parses = [](const json& value) { return parse_lighting_from_text(value.dump()).has_value(); };
    for (const auto& [section, key, value] : std::initializer_list<std::tuple<const char*, const char*, double>>{
             {"sky", "rainbow_radius_deg", 0.0}, {"sky", "rainbow_secondary_share", 1.5}, {"sky", "rainbow_radius_deg", 60.0},
             {"weather", "overcast_sun", 1.5}, {"weather", "overcast_haze_distance", 0.0}, {"weather", "wet_gloss_power", 0.0},
             {"rain", "max_drops", 0.0}, {"rain", "max_drops", 1e6}, {"rain", "volume_size", 0.0}, {"rain", "heavy_alpha", 2.0},
             {"wind", "max_specks", -1.0}, {"wind", "flag_full_speed", 0.1}, {"wind", "speck_calm_share", 1.5}}) {
        json bad = shipped;
        bad[section][key] = value;
        CHECK(!parses(bad));
    }
    CHECK(parses(shipped));
}

TEST_CASE("lightning strikes only while it thunders and play allows, and its thunder comes later") {
    const lightning_style& style = fixture_palette().lightning;
    const float dt = 1.0f / 30.0f;
    const auto run = [&](const float thunder, const bool allowed, const float seconds) {
        lightning_presentation state;
        int strikes = 0;
        int claps = 0;
        bool striking = false;
        for (float t = 0.0f; t < seconds; t += dt) {
            state = update_lightning(state, thunder, allowed, style, dt);
            strikes += state.strike && !striking ? 1 : 0;
            striking = state.strike.has_value();
            claps += state.close_claps + state.far_claps;
        }
        return std::make_pair(strikes, claps);
    };
    CHECK(run(0.0f, true, 120.0f).first == 0);
    CHECK(run(1.0f, false, 120.0f).first == 0);
    const auto [strikes, claps] = run(1.0f, true, 600.0f);
    // About strikes_per_minute at full thunder, each heard once.
    const float expected = style.strikes_per_minute * 10.0f;
    CHECK(static_cast<float>(strikes) > 0.5f * expected);
    CHECK(static_cast<float>(strikes) < 1.5f * expected);
    CHECK(claps >= strikes - 3);
    CHECK(claps <= strikes);
    CHECK(run(0.3f, true, 600.0f).first < strikes);
}

TEST_CASE("a lightning flash flickers and is gone by flash_seconds") {
    const lightning_style& style = fixture_palette().lightning;
    for (const float seed : {1.0f, 23.0f, 57.5f}) {
        CHECK(lightning_flash(0.0f, seed, style) == 1.0f);
        CHECK(lightning_flash(style.flash_seconds, seed, style) == 0.0f);
        for (float age = 0.0f; age < style.flash_seconds; age += 0.01f) {
            const float flash = lightning_flash(age, seed, style);
            CHECK(flash >= 0.0f);
            CHECK(flash <= 1.0f);
        }
    }
    CHECK(make_render_lightning(lightning_presentation{}, style).flash == 0.0f);
}
