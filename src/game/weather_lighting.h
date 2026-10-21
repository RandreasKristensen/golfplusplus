#pragma once

// The light under the weather: a lighting key (game/sun_lighting.h) with the
// sky's cover, the rain and the fog on it, the haze they thicken and the wet ground,
// by the palette's weather_style. Visual only, like the time of day: the
// amounts are the weather as drawn, faded in and out (core/weather_frame.h).

#include "game/lighting_loader.h"

#include <glm/vec3.hpp>

// The weather as drawn, each 0..1: the sky's cover, the rain, the fog, a
// sun shower's rainbow, the ground's wetness and the thunder.
struct shown_weather {
    float cloud = 0.0f;
    float rain = 0.0f;
    float fog = 0.0f;
    float rainbow = 0.0f;
    float wetness = 0.0f;
    float thunder = 0.0f;
};

// How much of the sun's light the weather takes, as a cloud cover 0..1: the
// clouds', or the fog's (weather_style::fog_cover) where that is more.
float sky_cover(const weather_style& style, const shown_weather& weather);

// `clear` under `weather`'s cover, rain and fog; clear weather leaves it as it is.
lighting_key weathered_lighting(const lighting_key& clear, const weather_style& style, const shown_weather& weather);

// The share of their shadow the sun casts (`clear_share`) under the cover (sky_cover).
float weathered_sun_shadow_share(float clear_share, const weather_style& style, const shown_weather& weather);

// How the weather thickens the haze: `gain` of the way from the course's
// far haze to full, full by `distance_share` of its distance, its colour
// `grey` of the way to grey.
struct weather_haze {
    float gain = 0.0f;
    float distance_share = 1.0f;
    float grey = 0.0f;
};

weather_haze haze_under_weather(const weather_style& style, const shown_weather& weather);

// `color` `grey` of the way to its own luminance.
glm::vec3 greyed(const glm::vec3& color, float grey);

// How mottled the overcast sky is, as a share of the sky panorama's cloud
// contrast: positive keeps its bright clouds bright, negative turns them
// into rain's dark patches.
float overcast_mottle(const weather_style& style, const shown_weather& weather);
