#pragma once

// The ground's coarser levels (renderer/render_mesh_lod.h) are drawn only in
// water mirrors, where they must show what the full ground shows: a pond's
// banks seen from four sides and two distances through the game's whole
// renderer, with the shipped levels and with the full ground the oracle. Too
// many pixels may not differ by more than a visible step; and the same views
// drawn at the loosest error the picture data allows (8 pixels) must fail
// that test, so the check can see a level that is too coarse. Failures write
// the pictures.

#include "check_support.h"

#include <SDL.h>

#include <filesystem>
#include <string>

struct terrain_lod_check_result {
    bool ran = false;
    int pictures = 0;
    int failures = 0;
};

terrain_lod_check_result check_terrain_lod(const check_course& course, const game_content& content, const lighting_palette& palette,
                                           const std::string& asset_root, SDL_Window* window, const std::filesystem::path& out);
