#include "game/weather_lighting.h"

#include "game/lighting_fields.h"
#include "physics/vector_math.h"

#include <algorithm>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
// Koschmieder's law: air in which a dark thing fades to the 2% contrast an
// eye just sees at `visibility` metres takes -ln 0.02 / visibility per metre.
constexpr float koschmieder_contrast = 3.912f;

float luminance(const glm::vec3& color) {
    return glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
}
}

glm::vec3 greyed(const glm::vec3& color, const float grey) {
    return glm::mix(color, glm::vec3(luminance(color)), clamp01(grey));
}

float sky_cover(const weather_style& style, const shown_weather& weather) {
    return std::max(clamp01(weather.cloud), style.fog_cover * clamp01(weather.fog));
}

lighting_key weathered_lighting(const lighting_key& clear, const weather_style& style, const shown_weather& weather) {
    const float cloud = sky_cover(style, weather);
    const float rain = clamp01(weather.rain);
    const float kept = glm::mix(1.0f, style.rain_light, rain);
    const float grey = style.overcast_grey * cloud;
    const glm::vec3 sun = clear.sun_color * glm::mix(1.0f, style.overcast_sun, cloud);
    const glm::vec3 scattered = (clear.sun_color - sun) * style.overcast_scatter;

    lighting_key key = clear;
    key.sun_color = sun * kept;
    key.ambient_sky = greyed(clear.ambient_sky + scattered, grey) * kept;
    key.ambient_ground = greyed(clear.ambient_ground, grey) * kept;
    const glm::vec3 overcast =
        glm::min(style.overcast_sky * (style.overcast_sky_brightness * luminance(clear.sky_zenith)), glm::vec3(1.0f));
    key.sky_zenith = glm::mix(clear.sky_zenith, overcast, cloud) * kept;
    key.sky_blend = glm::mix(clear.sky_blend, std::max(clear.sky_blend, style.overcast_sky_blend), cloud);
    // Rain's curtains stay pale: its haze is not darkened.
    key.haze_tint = greyed(clear.haze_tint, grey);
    key.horizon_glow_strength = clear.horizon_glow_strength * glm::mix(1.0f, style.overcast_glow, cloud);
    key.exposure = clear.exposure * glm::mix(1.0f, style.overcast_exposure, cloud);
    key.saturation = clear.saturation * glm::mix(1.0f, style.overcast_saturation, cloud);
    key.gain_noise = std::min(1.0f, clear.gain_noise + style.rain_gain_noise * rain);
    key.mist_density = std::min(max_mist_density, clear.mist_density + style.fog_mist_density * clamp01(weather.fog) +
                                                      style.rain_mist_density * rain);
    return key;
}

float weathered_sun_shadow_share(const float clear_share, const weather_style& style, const shown_weather& weather) {
    return clear_share * glm::mix(1.0f, style.overcast_shadows, sky_cover(style, weather));
}

weather_haze haze_under_weather(const weather_style& style, const shown_weather& weather) {
    const float cloud = clamp01(weather.cloud);
    const float rain = clamp01(weather.rain);
    const float fog = clamp01(weather.fog);
    weather_haze haze;
    haze.gain = clamp01(style.overcast_haze * cloud + style.rain_haze * rain + style.fog_haze * fog);
    haze.distance_share = glm::mix(1.0f, style.overcast_haze_distance, cloud) * glm::mix(1.0f, style.rain_haze_distance, rain) *
        glm::mix(1.0f, style.fog_haze_distance, fog);
    haze.grey = std::max(style.overcast_grey * cloud, style.fog_grey * fog);
    return haze;
}

air_scattering scattering_under_weather(const weather_style& style, const shown_weather& weather) {
    const float clear = koschmieder_contrast / style.clear_visibility_meters;
    const float rain = clamp01(weather.rain) * koschmieder_contrast / style.rain_visibility_meters;
    const float fog = clamp01(weather.fog) * koschmieder_contrast / style.fog_visibility_meters;
    air_scattering air;
    air.per_meter = clear + rain + fog;
    air.forwardness =
        (clear * style.clear_forwardness + rain * style.rain_forwardness + fog * style.fog_forwardness) / air.per_meter;
    return air;
}

float overcast_mottle(const weather_style& style, const shown_weather& weather) {
    return glm::mix(style.overcast_mottle, -style.rain_mottle, clamp01(weather.rain)) * (1.0f - clamp01(weather.fog));
}
