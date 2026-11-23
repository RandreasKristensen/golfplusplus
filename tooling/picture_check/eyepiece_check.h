#pragma once

// The rangefinder's eyepiece throws away the circle of the main picture and
// the corners of the zoom's square, and the renderer does not shade them
// (renderer/eyepiece_pass.h). Held up at the course's start in four directions
// and, with water, at its largest pond from four banks, under noon and night,
// with and without ponds drawn, each picture the eyepiece
// makes is drawn with that saving on and off through the game's whole
// renderer; every pixel must be bit-identical. Failures write both pictures.

#include "check_support.h"

#include <SDL.h>

#include <filesystem>
#include <string>

struct eyepiece_check_result {
    bool ran = false;
    int pictures = 0;
    int failures = 0;
};

eyepiece_check_result check_eyepiece(const check_course& course, const game_content& content, const lighting_palette& palette,
                                     const std::string& asset_root, SDL_Window* window, const std::filesystem::path& out);
