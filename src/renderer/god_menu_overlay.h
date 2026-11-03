#pragma once

// The god menu (core/god_menu.h) as drawn: a HUD panel down the left, one
// row per held value. The look tuning panel (core/look_tuning.h) is drawn
// the same way, one row per number. GL-free.

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
