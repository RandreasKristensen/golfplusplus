#pragma once

// Repeatable renderer throughput, frame-time tails and GPU-stage measurements.
// Optional component removal is diagnostic only. No settings, saves or content
// are written; frame and summary CSVs live in the picture-check output.
#include "check_support.h"
#include <SDL.h>

bool profile_picture(const check_course& course,const game_content& content,const lighting_palette& palette,
                     const std::string& root,const std::filesystem::path& out,SDL_Window* window,
                     bool isolate_components = false, bool night = false);
