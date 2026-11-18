#pragma once

// A tree above the main view must still appear in its pond's reflection.
// Uses the same planar pass, full tree model and water shader as play.
#include "check_support.h"
#include "renderer/backdrop_pass.h"
#include "renderer/lighting_uniforms.h"
#include "renderer/water_renderer.h"

bool check_water_scene(const check_course& course, const lighting_palette& palette, const std::string& root,
                       const std::filesystem::path& out, backdrop_pass& backdrop, water_renderer& water,
                       lighting_uniforms& lighting, framebuffer& scene, gl_timer_pool& timers);
