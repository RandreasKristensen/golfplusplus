#pragma once

// The god menu (core/god_menu.h) as drawn: a HUD panel down the left, one
// row per held value. GL-free.

#include "game/text_assets.h"
#include "renderer/overlay_batch.h"

#include <string>
#include <vector>

struct render_god_menu_row {
    std::string label;
    std::string value;
    bool selected = false;
    bool held = false;  // not live: drawn amber, live grey
};

struct render_god_menu {
    std::string title;
    std::vector<render_god_menu_row> rows;
    std::string note;  // the keys, or that online the server holds the weather
};

void draw_god_menu(overlay_batch& batch, const text_assets& text, const render_god_menu& menu);
