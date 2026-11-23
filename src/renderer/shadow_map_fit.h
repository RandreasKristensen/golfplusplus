#pragma once

// Where the shadow map looks from (renderer/shadow_map_pass.h): an
// orthographic view along the sun over a square of ground around the camera.
// The light's rotation depends only on the sun, and the square's corner is
// snapped to whole texels of it, so the map's texels stay put on the ground
// as the camera moves and shadow edges never shimmer. GL-free.

#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

struct shadow_map_fit {
    glm::mat4 view_proj{1.0f};  // world to the map's clip space
    float depth_range = 0.0f;   // metres along the sun its depth spans
    // Which map it is, apart from the sun: the snapped corner in light space, and where its depth is centred along
    // the sun. A map drawn for another eye is as good while these match and the depth has moved less than an extent.
    glm::vec2 corner{0.0f};
    float depth_middle = 0.0f;
};

// World to light space, no translation: x and y across the sun, z towards it.
glm::mat3 shadow_light_rotation(const glm::vec3& towards_sun);

// The map `resolution` texels across over `extent` metres square, looking
// down `towards_sun` (unit), centred on `eye`. Camera orientation is not an
// input: turning must not move the map's coverage or its shadow fade.
shadow_map_fit fit_shadow_map(const glm::vec3& towards_sun, const glm::vec3& eye, float extent, int resolution);
