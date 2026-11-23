#pragma once

// Bare crowns and falling leaves drawn by the renderer: falling_leaf.vert
// replays renderer/falling_leaf.h (every visible leaf where the function puts
// it, nothing where it puts none), and a bare crown watched by a walking eye
// changes from frame to frame no more than a picture of twice the lines does
// (the grass's motion bound), so its fine twigs do not shimmer.
#include "check_support.h"
#include "game/tree_season.h"
// Also writes each species' summer and winter pictures, near and far, for a person to look at.
bool check_tree_seasons(const lighting_palette& palette, const tree_tuning& tuning, const std::string& root);
