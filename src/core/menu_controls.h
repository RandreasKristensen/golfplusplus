#pragma once

// What every menu screen does the same way: moving the selection with
// arrows or the mouse, adding tiles to the render data, and changing
// screens. Shared by core/startup_flow and core/online_menus.

#include "core/input.h"
#include "core/startup_flow.h"
#include "game/text_assets.h"
#include "renderer/menu_overlay.h"

#include <optional>
#include <string>
#include <vector>

#include <glm/vec2.hpp>

inline constexpr int picker_columns = 3;

// Moves `selection` by arrow keys (left/right by one, up/down by a row of
// `columns`) or onto a clicked tile; queues a move sound when it changed.
// Returns the clicked tile, or -1.
int select_with_input(startup_menu_screen screen,
                      int columns,
                      int& selection,
                      int count,
                      const input_state& input,
                      std::optional<glm::vec2> click,
                      std::vector<ui_sound>& sounds);

void add_tile(render_startup_menu& menu, std::string title, std::string subtitle, bool selected);
// One tile per catalog course, with hole 1's preview.
void add_course_tiles(render_startup_menu& menu, const startup_catalog& catalog, const text_assets& text, int selection);

// Shows `flow` from its first tile, with no message, field or wait left
// over from the screen before.
void open_screen(startup_flow_state& state, startup_flow flow);
