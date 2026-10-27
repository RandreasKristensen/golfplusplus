#include "core/sky_frame.h"

#include "core/weather_frame.h"
#include "game/mode_dispatch.h"
#include "game/sky_clock.h"
#include "game/sun_lighting.h"

#include <cmath>

#include <glm/trigonometric.hpp>

namespace {
// Holding the scrub key runs the sky this fast.
constexpr double scrub_game_days_per_second = 2.0 / 24.0;
}

sky_controls scrub_sky(sky_controls controls, const bool held, const float dt) {
    if (held) {
        controls.scrub_game_days += scrub_game_days_per_second * static_cast<double>(dt);
    }
    return controls;
}

double frame_game_day(const sky_timeline& timeline, const course_sky& sky, const sky_controls& controls,
                      const std::int64_t now_unix_ms) {
    const std::int64_t date = controls.pinned_date ? *controls.pinned_date : course_date(timeline, sky);
    const double offset = static_cast<double>(sky.utc_offset_hours);
    // The date's UTC noon is that local date on any clock within 12 hours of UTC.
    const double clock = controls.pinned_hour
        ? game_day_at_local_time(static_cast<double>(date) + 0.5, offset, static_cast<double>(*controls.pinned_hour))
        : game_day_at(timeline, now_unix_ms);
    return game_day_on_date(clock + controls.scrub_game_days, date, offset);
}

double course_game_day(const game_state& game, const sky_controls& controls, const std::int64_t local_unix_ms) {
    return frame_game_day(game.tuning.sky_clock, game.course.sky, controls, shared_unix_ms(game, local_unix_ms));
}

float course_hour(const game_state& game, const sky_controls& controls, const std::int64_t local_unix_ms) {
    const double local_day = course_game_day(game, controls, local_unix_ms) + static_cast<double>(game.course.sky.utc_offset_hours) / 24.0;
    return static_cast<float>((local_day - std::floor(local_day)) * 24.0);
}

render_lighting make_render_lighting(const game_state& game, const lighting_palette& palette, const sky_controls& controls,
                                     const std::int64_t local_unix_ms, const shown_weather& weather) {
    const double latitude = static_cast<double>(game.world.latitude_deg);
    const double longitude = static_cast<double>(game.world.longitude_deg);
    const double game_day = course_game_day(game, controls, local_unix_ms);
    const sky_state sky = with_lowest_sun(compute_sky_state(game_day, latitude, longitude), game.course.sky.lowest_sun_deg);
    const float sun_elevation_deg = glm::degrees(sky.sun_elevation);
    const lighting_key light = weathered_lighting(lighting_at(palette, sun_elevation_deg), palette.weather, weather);

    render_lighting lighting;
    lighting.sun_direction = sky.sun_direction;
    lighting.sun_color = light.sun_color;
    lighting.ambient_sky = light.ambient_sky;
    lighting.ambient_ground = light.ambient_ground;
    lighting.sky_zenith = light.sky_zenith;
    lighting.sky_blend = light.sky_blend;
    lighting.haze_tint = light.haze_tint;
    lighting.horizon_glow = light.horizon_glow;
    lighting.horizon_glow_strength = light.horizon_glow_strength;
    lighting.moon_direction = sky.moon_direction;
    lighting.moon_phase = sky.moon_phase;
    lighting.star_visibility = star_visibility(palette.sky, sun_elevation_deg);
    lighting.shadows.sun_share =
        weathered_sun_shadow_share(sun_shadow_share(palette.shadows, sun_elevation_deg), palette.weather, weather);
    lighting.shadows.night_share = palette.shadows.night_share;
    lighting.shadows.max_stretch = palette.shadows.max_stretch;
    lighting.shadows.horizon_softness = std::sin(glm::radians(palette.shadows.horizon_softness_deg));
    lighting.night_lights = night_light_share(palette.night_lights, sun_elevation_deg);
    lighting.exposure = light.exposure;
    lighting.white_balance = light.white_balance;
    lighting.gain_noise = light.gain_noise;
    lighting.saturation = light.saturation;
    lighting.weather = make_render_sky_weather(weather, palette.weather);
    return lighting;
}
