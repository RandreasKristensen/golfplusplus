#pragma once

// The grass field's GPU tiles (grass_tile_store.h). Two parts:
//  - upload: tiles uploaded with a hostile pixel-unpack state land texel for
//    texel (odd 65-wide rows in each of the three formats), the state is
//    restored, the table names exactly what is resident, and an evicted tile
//    leaves no mapping behind;
//  - transitions: through the game's renderer, walks over many tiles, turns,
//    tile corners, zoom and a pond's mirror are drawn with every tile resident,
//    with a cache just big enough for one view (so tiles are evicted and
//    reloaded as the eye moves: the pictures must be identical), and with one
//    a third of that (drawn in batches: the same picture to within the order
//    blades blend in). Uploads are counted: a tile is sent once.

#include "check_support.h"

#include <SDL.h>

#include <filesystem>
#include <string>

struct grass_tile_check_result {
    bool ran = false;
    int failures = 0;
};

// `with_motion` also reruns the rough's motion check (grass_motion_check.h) with a
// cache that draws every frame in batches and requires the same measures.
grass_tile_check_result check_grass_tiles(const check_course& course, const game_content& content, const lighting_palette& palette,
                                          const std::string& asset_root, SDL_Window* window, const std::filesystem::path& out,
                                          bool with_motion);
