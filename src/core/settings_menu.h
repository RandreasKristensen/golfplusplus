#pragma once

// The settings screen, shown from the main menu and from the in-round menu:
// one tile per setting definition (game/settings.h), in file order, then
// BACK. Up and down move, left and right change the selected setting by
// its step. Adding a setting is adding it to assets/ui/settings.json (and
// applying it in app); this screen lists it without changes.

#include "core/input.h"
#include "core/startup_flow.h"
#include "game/settings.h"
#include "game/text_assets.h"
#include "renderer/menu_overlay.h"

#include <optional>
#include <vector>

#include <glm/vec2.hpp>

struct settings_menu_result {
    bool changed = false;  // a value changed: apply and save the settings
    bool back = false;     // leave the screen
};

// `selection` indexes the definitions, then BACK. `click` is in overlay clip space.
settings_menu_result update_settings_menu(int& selection,
                                          settings_values& values,
                                          const std::vector<setting_definition>& definitions,
                                          const input_state& input,
                                          std::optional<glm::vec2> click,
                                          std::vector<ui_sound>& sounds);

render_startup_menu make_settings_menu_render_data(int selection,
                                                   const settings_values& values,
                                                   const std::vector<setting_definition>& definitions,
                                                   const text_assets& text);
