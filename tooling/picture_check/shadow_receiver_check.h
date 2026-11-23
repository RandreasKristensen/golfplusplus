#pragma once

// Every shader that reads the sun's shadow map must be told its texture unit.
// After the game's whole renderer has drawn a frame, each linked program with
// a `u_shadow_map` must hold the shadow unit. Trees and rough in sun and low
// sun are drawn into `out` for a person to look at (`shadow_<case>_<side>.bmp`).

#include "check_support.h"

#include <SDL.h>

#include <filesystem>
#include <string>

struct shadow_receiver_result {
    int programs = 0;  // programs with a u_shadow_map
    int unset = 0;     // of those, the ones not on the shadow unit
    int pictures = 0;
    bool ran = false;
};

shadow_receiver_result check_shadow_receivers(const check_course& course, const game_content& content,
                                              const lighting_palette& palette, const std::string& asset_root,
                                              SDL_Window* window, const std::filesystem::path& out);
