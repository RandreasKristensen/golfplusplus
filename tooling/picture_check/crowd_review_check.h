#pragma once

// A crowd of figures, balls and carts for a person to compare before and after
// a change to how they are drawn. On the course, a standing group, a close one
// and, with water, one on a pond's far bank are captured through the game's
// whole renderer at 360 lines under noon, low sun and night: the scene before
// picture processing as raw floats and a BMP in `out`, named
// `<label>_<view>_<case>.f32|bmp`. With `against` (another label's run) each
// is compared exactly and the differing pixels and the largest difference
// printed; the run fails when a picture is missing.

#include "check_support.h"

#include <SDL.h>

#include <filesystem>
#include <string>

struct crowd_review_options {
    std::string label;
    std::string against;  // empty: just capture
    std::filesystem::path out;
};

bool run_crowd_review(const check_course& course, const game_content& content, const lighting_palette& palette,
                      const std::string& asset_root, SDL_Window* window, const crowd_review_options& options);
