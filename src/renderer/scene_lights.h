#pragma once

// Which of the frame's lights the scene is lit by: lighting.glsl evaluates a
// fixed few per pixel, so the CPU keeps the ones nearest the camera. GL-free.

#include "renderer/render_data.h"

#include <cstddef>
#include <vector>

#include <glm/vec3.hpp>

inline constexpr std::size_t max_scene_lights = 16;  // lighting.glsl's max_lights

// At most max_scene_lights of `lights`, nearest first: by how far `eye` is
// from the edge of each one's reach, so a long headlight beam counts as near
// while it reaches the camera, then by how far from the light itself. Lights
// that light nothing are left out; ties keep their order.
std::vector<render_light> nearest_scene_lights(const std::vector<render_light>& lights, const glm::vec3& eye);
