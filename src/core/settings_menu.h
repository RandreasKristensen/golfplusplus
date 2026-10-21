#pragma once

// The settings screen, shown from the main menu and from the in-round menu:
// one tile per setting definition (game/settings.h), in file order, a few at
// a time in a list that scrolls, then BACK under it. Typing filters the list
// by the settings' names, and backspace takes the search back. Up and down move, left and right change the
// selected setting by its step; the mouse selects by hovering, a click on a
// tile's left half (its "<") steps down and on its right half (its ">") up,
// and the wheel scrolls. Adding a setting is adding it to
// assets/interface/settings.json (and applying it in app); this screen lists it
// without changes.

#include "core/input.h"
#include "core/startup_flow.h"
#include "game/settings.h"
#include "game/text_assets.h"
#include "renderer/menu_overlay.h"

#include <string>
#include <vector>

struct settings_menu_result {
    bool changed = false;  // a value changed: apply and save the settings
    bool back = false;     // leave the screen
};

// What `labels` (each definition's name, as shown) a search keeps: the
// definitions' indices, in order.
std::vector<int> matching_settings(const std::vector<std::string>& labels, const std::string& search);

settings_menu_result update_settings_menu(settings_view& view,
                                          settings_values& values,
                                          const std::vector<setting_definition>& definitions,
                                          const std::vector<std::string>& labels,
                                          const input_state& input,
                                          const menu_pointer& pointer,
                                          std::vector<ui_sound>& sounds);

render_startup_menu make_settings_menu_render_data(const settings_view& view,
                                                   const settings_values& values,
                                                   const std::vector<setting_definition>& definitions,
                                                   const text_assets& text);
