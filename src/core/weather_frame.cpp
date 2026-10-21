#include "core/weather_frame.h"

#include "physics/vector_math.h"
#include "physics/wind.h"

#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

namespace {
float smooth01(const float t) {
    const float x = clamp01(t);
    return x * x * (3.0f - 2.0f * x);
}

float wrapped_phase(const float phase) {
    return std::fmod(phase, glm::two_pi<float>());
}

int share_of(const int count, const float share) {
    return static_cast<int>(std::lround(static_cast<float>(count) * clamp01(share)));
}
}

shown_weather shown_target(const weather_state& weather) {
    return shown_weather{clamp01(weather.cloud), clamp01(weather.rain), clamp01(weather.fog), clamp01(weather.rainbow),
                         clamp01(weather.wetness), clamp01(weather.thunder)};
}

float eased_toward(const float shown, const float target, const float dt, const float seconds) {
    if (seconds <= 0.0f) {
        return target;
    }
    return shown + (target - shown) * (1.0f - std::exp(-std::max(0.0f, dt) / seconds));
}

glm::vec3 wrapped_in_volume(const glm::vec3& offset, const glm::vec3& size) {
    return offset - size * glm::floor(offset / size);
}

glm::vec3 rain_volume(const rain_style& style) {
    return glm::vec3(style.volume_size);
}

glm::vec3 speck_volume(const wind_style& style) {
    return glm::vec3(style.speck_volume_size, 2.0f * style.speck_height, style.speck_volume_size);
}

glm::vec3 rain_velocity(const weather_state& weather, const float rain, const rain_style& style, const wind_profile& profile) {
    const float fall = glm::mix(style.light_fall_speed, style.heavy_fall_speed, clamp01(rain));
    return horizontal(wind_at_height(weather.wind, style.wind_height, profile).velocity) - world_up * fall;
}

glm::vec3 speck_velocity(const weather_state& weather, const wind_style& style, const wind_profile& profile) {
    return horizontal(wind_at_height(weather.wind, style.speck_height, profile).velocity);
}

glm::vec3 flag_wind(const weather_state& weather, const float flag_height, const wind_profile& profile) {
    return horizontal(wind_at_height(weather.wind, flag_height, profile).velocity);
}

float flag_lift(const glm::vec3& wind, const wind_style& style) {
    return smooth01((glm::length(horizontal(wind)) - style.flag_calm_speed) / (style.flag_full_speed - style.flag_calm_speed));
}

weather_presentation update_weather_presentation(weather_presentation state, const weather_state& weather,
                                                 const lighting_palette& palette, const wind_profile& profile,
                                                 const float flag_height, const float dt, const bool strikes_allowed) {
    const shown_weather target = shown_target(weather);
    if (!state.started) {
        state.started = true;
        state.shown = target;
        return state;
    }
    const float seconds = palette.weather.fade_seconds;
    shown_weather& shown = state.shown;
    shown.cloud = eased_toward(shown.cloud, target.cloud, dt, seconds);
    shown.rain = eased_toward(shown.rain, target.rain, dt, seconds);
    shown.fog = eased_toward(shown.fog, target.fog, dt, seconds);
    shown.rainbow = eased_toward(shown.rainbow, target.rainbow, dt, seconds);
    shown.wetness = eased_toward(shown.wetness, target.wetness, dt, seconds);
    shown.thunder = eased_toward(shown.thunder, target.thunder, dt, seconds);
    state.lightning = update_lightning(state.lightning, shown.thunder, strikes_allowed, palette.lightning, dt);

    state.rain_offset = wrapped_in_volume(state.rain_offset + rain_velocity(weather, shown.rain, palette.rain, profile) * dt,
                                          rain_volume(palette.rain));
    state.speck_offset = wrapped_in_volume(state.speck_offset + speck_velocity(weather, palette.wind, profile) * dt,
                                           speck_volume(palette.wind));
    const glm::vec3 wind = flag_wind(weather, flag_height, profile);
    const float lift = flag_lift(wind, palette.wind);
    const float hz = glm::mix(palette.wind.flag_calm_hz, palette.wind.flag_full_hz, lift);
    state.flag_phase = wrapped_phase(state.flag_phase + glm::two_pi<float>() * hz * dt);
    // The specks tumble as fast as the flags flap.
    state.speck_tumble_phase = wrapped_phase(state.speck_tumble_phase + glm::two_pi<float>() * hz * dt);
    return state;
}

render_sky_weather make_render_sky_weather(const shown_weather& shown, const weather_style& style) {
    const weather_haze haze = haze_under_weather(style, shown);
    render_sky_weather sky;
    sky.cover = clamp01(shown.cloud);
    sky.mottle = overcast_mottle(style, shown);
    sky.rainbow = clamp01(shown.rainbow);
    sky.haze_gain = haze.gain;
    sky.haze_distance_share = haze.distance_share;
    sky.haze_grey = haze.grey;
    sky.wet_albedo = glm::mix(1.0f, style.wet_albedo, clamp01(shown.wetness));
    sky.wet_gloss = style.wet_gloss * clamp01(shown.wetness);
    sky.wet_gloss_power = style.wet_gloss_power;
    return sky;
}

render_weather make_render_weather(const weather_presentation& state, const weather_state& weather,
                                   const lighting_palette& palette, const wind_profile& profile, const float flag_height,
                                   const glm::vec3& camera, const float ground_height) {
    const shown_weather& shown = state.shown;
    const rain_style& rain = palette.rain;
    const wind_style& wind = palette.wind;
    render_weather out;
    out.rain.count = share_of(rain.max_drops, std::pow(clamp01(shown.rain), rain.drop_power));
    out.rain.center = camera;
    out.rain.size = rain_volume(rain);
    out.rain.offset = state.rain_offset;
    out.rain.velocity = rain_velocity(weather, shown.rain, rain, profile);
    out.rain.streak_seconds = rain.streak_seconds;
    out.rain.color = rain.color;
    out.rain.water = true;
    out.rain.alpha = glm::mix(rain.light_alpha, rain.heavy_alpha, shown.rain);
    out.rain.near_fade = rain.near_fade;

    const glm::vec3 specks = speck_velocity(weather, wind, profile);
    out.specks.count = share_of(wind.max_specks,
                                glm::mix(wind.speck_calm_share, 1.0f, smooth01(glm::length(specks) / wind.speck_full_speed)));
    out.specks.center = glm::vec3(camera.x, ground_height + wind.speck_height, camera.z);
    out.specks.size = speck_volume(wind);
    out.specks.offset = state.speck_offset;
    out.specks.velocity = specks;
    out.specks.streak_seconds = wind.speck_seconds;
    out.specks.color = wind.speck_color;
    out.specks.alpha = 1.0f;  // bits of grass and leaf are opaque
    out.specks.tumble = wind.speck_tumble;
    out.specks.tumble_phase = state.speck_tumble_phase;

    out.flag_wind = flag_wind(weather, flag_height, profile);
    out.flag_lift = flag_lift(out.flag_wind, wind);
    out.flag_phase = state.flag_phase;
    out.flag_flap = wind.flag_flap * out.flag_lift;
    out.lightning = make_render_lightning(state.lightning, palette.lightning);
    return out;
}

weather_presentation update_frame_weather(const weather_presentation& state, const game_state& game,
                                          const lighting_palette& palette, const render_data& data, const bool playing,
                                          const float dt) {
    weather_presentation running = state;
    running.started = state.started && playing;
    return update_weather_presentation(running, game.weather, palette, game.tuning.wind.profile, data.pin_visual_height_meters, dt,
                                       playing && game.mode == game_mode::walking);
}

render_weather make_frame_weather(const weather_presentation& state, const game_state& game, const lighting_palette& palette,
                                  const render_data& data) {
    return make_render_weather(state, game.weather, palette, game.tuning.wind.profile, data.pin_visual_height_meters,
                               data.camera_position, data.player_position.y);
}
