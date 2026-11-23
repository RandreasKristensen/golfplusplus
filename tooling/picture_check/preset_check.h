#pragma once

// Every scene resolution preset (picture.json scene_lines) at several window
// sizes, drawn through the game's whole renderer: the scene target and the
// window's picture must be the size renderer/picture_size.h says, whole
// window rows per scene line, and the scene must show something.

#include "check_support.h"

#include <SDL.h>

#include <string>

struct preset_check_result {
    bool ran = false;
    int pictures = 0;
    int failures = 0;
};

preset_check_result check_presets(const check_course& course, const game_content& content, const lighting_palette& palette,
                                  const std::string& asset_root, SDL_Window* window);
