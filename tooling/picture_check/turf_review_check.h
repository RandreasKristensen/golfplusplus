#pragma once

// Fairways and greens for a person to judge, and to measure. On the first
// course hole with a floodlit green, the putting, address, cup, approach and
// fairway views under noon, low sun, overcast, wet, night and wet night are
// captured through the game's whole renderer at 360 lines: every scene
// (before picture processing) and, for a few, the final TV picture, as BMPs in
// `out` named `<label>_<view>_<case>_scene|tv.bmp`, with `<label>_metrics.csv`
// of how bright and how clipped each is. Then the turf's motion
// (grass_motion_check.h) is measured over the fairway, the green's approach
// and, with water, a pond's reflection of both. `diagnose` also splits the
// night's brightness into its parts by taking each out in turn.

#include "check_support.h"

#include <SDL.h>

#include <filesystem>
#include <string>

struct turf_review_options {
    std::string label;
    std::filesystem::path out;
    bool diagnose = false;
};

// False when a picture could not be made or written. The motion measures are printed, not gated.
bool run_turf_review(const check_course& course, const game_content& content, const lighting_palette& palette,
                     const std::string& asset_root, SDL_Window* window, const turf_review_options& options);
