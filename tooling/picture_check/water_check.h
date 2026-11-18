#pragma once

// A wide open pond under a course's own backdrop, drawn by the game's
// backdrop and water passes from an eye 1.7 m over it, looking away from the
// sun at the horizon, in calm and wind, clear and overcast, low sun and
// night. Nothing on screen to reflect, so every reflection is the off-screen
// sky: the distant water, near the horizon, is compared with the backdrop it
// mirrors. It fails if the water is brighter than what it reflects, if a
// calm pond stops mirroring, or if wind leaves distant water nearly as bright
// as calm.

#include "check_support.h"

#include <filesystem>
#include <string>

struct water_check_result {
    int pictures = 0;
    int failures = 0;
    bool ran = false;
};

// Writes each picture, before the camcorder, into `out`.
water_check_result check_water(const check_course& course, const lighting_palette& palette, const std::string& asset_root,
                               const std::filesystem::path& out);
