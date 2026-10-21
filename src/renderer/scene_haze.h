#pragma once

// The haze the scene fades into with distance, so the far ground and trees
// melt into the course backdrop's land panorama instead of ending at a sharp
// edge. It rides in the frame_lighting block (lighting_uniforms.h): hazed
// shaders apply it with lighting.glsl's `hazed`, as
// amount = far_amount * min(distance / full_distance, 1)^2, and the backdrop
// fades its land into the same colour. GL-free.

#include "game/course_definition.h"
#include "game/weather_lighting.h"

#include <algorithm>

#include <glm/vec3.hpp>

struct scene_haze {
    glm::vec3 color{0.0f};
    // Haze at full_distance and beyond: the far ground's haze in the land
    // panorama, so the edge of the drawn ground meets it without a seam.
    float far_amount = 0.0f;
    float full_distance = 1.0f;
    glm::vec3 eye{0.0f};
};

// The haze over `backdrop` seen from `eye`: its colour times the time of
// day's `tint`, reaching the land panorama's far-ground haze
// (`far_ground_haze`, 0..1) by the edge of the play area (`area_extent` from
// its centre; 0 without one, which keeps the course's distance).
inline scene_haze make_scene_haze(const course_backdrop& backdrop, const float far_ground_haze, const glm::vec3& eye,
                                  const float area_extent, const glm::vec3& tint) {
    // Keeps the haze ramp from collapsing on a tiny play area.
    constexpr float min_haze_distance = 1.0f;
    scene_haze haze;
    haze.color = backdrop.haze_color * tint;
    haze.far_amount = std::clamp(far_ground_haze * backdrop.haze_amount, 0.0f, 1.0f);
    const float distance = area_extent > 0.0f ? std::min(backdrop.haze_distance, area_extent) : backdrop.haze_distance;
    haze.full_distance = std::max(min_haze_distance, distance);
    haze.eye = eye;
    return haze;
}

// `haze` thickened by the weather (weather_haze in game/weather_lighting.h).
inline scene_haze weathered_haze(scene_haze haze, const weather_haze& weather) {
    haze.color = greyed(haze.color, weather.grey);
    haze.far_amount = haze.far_amount + (1.0f - haze.far_amount) * std::clamp(weather.gain, 0.0f, 1.0f);
    haze.full_distance = std::max(1.0f, haze.full_distance * weather.distance_share);
    return haze;
}
