#include "game/weather_lighting.h"

#include "physics/vector_math.h"

#include <algorithm>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
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

float overcast_mottle(const weather_style& style, const shown_weather& weather) {
    return glm::mix(style.overcast_mottle, -style.rain_mottle, clamp01(weather.rain)) * (1.0f - clamp01(weather.fog));
}
