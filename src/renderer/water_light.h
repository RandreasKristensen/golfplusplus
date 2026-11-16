#pragma once

// The light through a pond's water on the CPU, as water.frag works it out, so
// tests can say what the water takes out and what it adds: Beer-Lambert
// absorption per colour channel, the water's own colour replacing what is
// lost along the path, Schlick's Fresnel for the surface, and Snell's window seen from
// under it (water.frag, underwater.frag). GL-free.

#include <algorithm>
#include <cmath>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "game/lighting_loader.h"

// Reflectance of water (n = 1.33) looking straight down.
inline constexpr float water_f0 = 0.02f;

// How far light goes through the water: no further than the style's cap.
inline float water_path_meters(const water_style& style, const float meters) {
    return std::clamp(meters, 0.0f, style.max_path_meters);
}

// The share of each colour that survives `path_meters` of water.
inline glm::vec3 water_transmittance(const water_style& style, const float path_meters) {
    const glm::vec3 absorption(style.absorption_red, style.absorption_green, style.absorption_blue);
    return glm::vec3(std::exp(-absorption.x * path_meters), std::exp(-absorption.y * path_meters),
                     std::exp(-absorption.z * path_meters));
}

// What the eye sees of `beneath` through `path_meters` of water lit by `light`:
// what each channel loses is replaced by the water's own colour, so a long
// path converges to scatter_color * light.
inline glm::vec3 water_seen_through(const water_style& style, const glm::vec3& beneath, const glm::vec3& light,
                                    const float path_meters) {
    const glm::vec3 kept = water_transmittance(style, path_meters);
    return beneath * kept + style.scatter_color * light * (glm::vec3(1.0f) - kept);
}

// How much of the sky the surface reflects, seen at `cos_view` (the cosine
// of the angle between the view and the surface's normal).
inline float water_fresnel(const float cos_view) {
    const float away = 1.0f - std::clamp(cos_view, 0.0f, 1.0f);
    return water_f0 + (1.0f - water_f0) * away * away * away * away * away;
}

// Water's refractive index, and the cosine (to straight up) of the critical
// angle past which a view from under the surface is reflected back down.
inline constexpr float water_refractive_index = 1.33f;
inline float water_critical_cosine() {
    return std::sqrt(1.0f - 1.0f / (water_refractive_index * water_refractive_index));
}

// How far either side of the critical cosine Snell's window blurs.
inline constexpr float water_window_blur = 0.04f;

// How much of the view from under the surface, `cos_up` from straight up
// (cosine of the angle to the normal), is inside Snell's window: 1 within the
// critical angle, 0 past it, smooth over the blur.
inline float water_snell_window(const float cos_up) {
    const float t = std::clamp((cos_up - (water_critical_cosine() - water_window_blur)) / (2.0f * water_window_blur), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Fresnel for light leaving the water (Schlick on the angle it leaves at):
// 0.02 straight up, all of it at the critical angle.
inline float water_fresnel_from_below(const float cos_up) {
    const float c = std::clamp(cos_up, 0.0f, 1.0f);
    const float leaving = std::sqrt(std::max(1.0f - water_refractive_index * water_refractive_index * (1.0f - c * c), 0.0f));
    return water_fresnel(leaving);
}

// The share of the view from under the surface that is what lies above it
// rather than the water's own murk reflected.
inline float water_see_above_share(const float cos_up) {
    return water_snell_window(cos_up) * (1.0f - water_fresnel_from_below(cos_up));
}

// The share of each colour of the light at the surface that reaches
// `depth_meters` under it.
inline glm::vec3 water_light_at_depth(const water_style& style, const float depth_meters) {
    return water_transmittance(style, std::max(depth_meters, 0.0f));
}

// The part of the segment from `from` to `to` (screen positions, -1 to 1 each
// way) inside the screen, as shares of it: nothing to march when low >= high.
struct water_screen_span {
    float low = 0.0f;
    float high = 0.0f;
};

inline water_screen_span water_reflection_screen_span(const glm::vec2& from, const glm::vec2& to) {
    glm::vec2 d = to - from;
    for (int axis = 0; axis < 2; ++axis) {
        if (std::abs(d[axis]) < 1.0e-6f) {
            d[axis] = 1.0e-6f;
        }
    }
    water_screen_span span{0.0f, 1.0f};
    for (int axis = 0; axis < 2; ++axis) {
        const float first = (-1.0f - from[axis]) / d[axis];
        const float second = (1.0f - from[axis]) / d[axis];
        span.low = std::max(span.low, std::min(first, second));
        span.high = std::min(span.high, std::max(first, second));
    }
    return span;
}

// How far a reflection found at screen position `uv` (0 to 1 each way) is
// trusted: 1 inside, falling smoothly to 0 at the picture's edge over
// `fade_share` of it.
inline float water_reflection_edge_confidence(const float u, const float v, const float fade_share) {
    const auto fade = [fade_share](const float x) {
        const float t = std::clamp(std::min(x, 1.0f - x) / fade_share, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    return std::min(fade(u), fade(v));
}

// How rough the wind makes open water, 0 a mirror .. 1 no reflection left
// whole: rising from calm to rough wind (Beaufort 1 to 3) as a smoothstep.
inline float water_wind_roughness(const water_style& style, const float wind_meters_per_second) {
    const float span = std::max(style.rough_wind_meters_per_second - style.calm_wind_meters_per_second, 0.01f);
    const float t = std::clamp((wind_meters_per_second - style.calm_wind_meters_per_second) / span, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// The share of the waves grown over `fetch_meters` of open water upwind:
// fetch-limited wave height goes as the square root of the fetch.
inline float water_fetch_share(const water_style& style, const float fetch_meters) {
    return std::sqrt(std::clamp(fetch_meters / style.full_fetch_meters, 0.0f, 1.0f));
}

// How rough the surface is at a point, calm_ripple_share .. 1: the wind's
// roughness grown over its fetch, `gust` (0 .. 1, between and in a gust)
// leaving gust_share of it between gusts.
inline float water_roughness(const water_style& style, const float wind_meters_per_second, const float fetch_meters,
                             const float gust) {
    const float gusting = style.gust_share + (1.0f - style.gust_share) * std::clamp(gust, 0.0f, 1.0f);
    const float windy = water_wind_roughness(style, wind_meters_per_second) * water_fetch_share(style, fetch_meters) * gusting;
    return style.calm_ripple_share + (1.0f - style.calm_ripple_share) * windy;
}

// The reflection's steps along its ray: `steps` of them from `first_meters`,
// each `growth` times the last, reaching `reach_meters` together, so near
// steps are fine and far ones, which cover few pixels, long. The growth by
// bisection; 1 when even steps of first_meters reach.
inline constexpr int water_reflection_steps = 32;

inline float water_reflection_step_growth(const float first_meters, const float reach_meters, const int steps) {
    const auto reached = [&](const float growth) {
        return first_meters * (std::pow(growth, static_cast<float>(steps)) - 1.0f) / (growth - 1.0f);
    };
    if (first_meters * static_cast<float>(steps) >= reach_meters) {
        return 1.0f;
    }
    float low = 1.0001f;
    float high = 4.0f;
    for (int i = 0; i < 48; ++i) {
        const float middle = 0.5f * (low + high);
        (reached(middle) > reach_meters ? high : low) = middle;
    }
    return high;
}

// Fetch is kept for this many directions the wind blows towards, the k-th
// at k * 45 degrees from +X towards +Z.
inline constexpr int water_fetch_directions = 8;

// The fetch for wind blowing towards `wind` (level), between the two nearest
// kept directions. Mirrors water.vert.
inline float water_fetch_toward(const float (&fetch)[water_fetch_directions], const glm::vec2& wind) {
    constexpr float slot_radians = 6.2831853f / static_cast<float>(water_fetch_directions);
    float slot = std::atan2(wind.y, wind.x) / slot_radians;
    slot -= std::floor(slot / static_cast<float>(water_fetch_directions)) * static_cast<float>(water_fetch_directions);
    const int low = static_cast<int>(std::floor(slot)) % water_fetch_directions;
    const int high = (low + 1) % water_fetch_directions;
    const float t = slot - std::floor(slot);
    return fetch[low] + (fetch[high] - fetch[low]) * t;
}
