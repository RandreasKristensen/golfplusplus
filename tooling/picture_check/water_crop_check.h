#pragma once

// A pond's mirror cropped to what its water looks up must give the picture the
// whole-picture mirror gives (the oracle: the same candidates with their
// rectangles widened to the whole picture), at calm and maximum wind, from near
// the water and high above it, at the corners, grazing, turning fast and over
// ponds at two heights, with trees inside and outside the main view.
#include "check_support.h"
#include "renderer/backdrop_pass.h"
#include "renderer/lighting_uniforms.h"
#include "renderer/water_renderer.h"

bool check_water_crops(const check_course& course, const lighting_palette& palette, const std::string& root,
                       const std::filesystem::path& out, backdrop_pass& backdrop, water_renderer& water,
                       lighting_uniforms& lighting, framebuffer& scene, gl_timer_pool& timers);
