#pragma once

// The rough's blades drawn straight down by the game's own grass renderer,
// round every kind of bare ground of a course (bunkers, greens, fairways'
// edges, tee boxes, cart paths) from eyes that put it in each ring, against
// the same ground drawn exactly: a blade whose root is on bare ground, or
// any blade over a tee box, fails.

#include "check_support.h"

#include <filesystem>
#include <string>

struct grass_check_result {
    int pictures = 0;
    long long blade_pixels = 0;
    long long wrong_pixels = 0;
    long long changed_roots = 0;
    long long checked_roots = 0;
    bool ran = false;
};

// Writes a picture of the worst place into `out` when any blade is wrong.
grass_check_result check_grass(const check_course& course, const lighting_palette& palette, const std::string& asset_root,
                               const std::filesystem::path& out);
