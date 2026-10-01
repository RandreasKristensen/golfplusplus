#pragma once

// Startup menus (main, help, hole/course pickers, confirm) drawn into the
// overlay batch. GL-free. The tile layout functions are shared with the menu
// flow's mouse hit test (core/startup_flow) so the two can't drift apart.

#include <optional>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "game/text_assets.h"
#include "physics/material_zone.h"
#include "renderer/overlay_batch.h"

enum class startup_menu_screen {
    none,
    main,
    help,
    hole_picker,
    course_picker,
    confirm  // laid out like main
};

struct render_hole_preview {
    glm::vec3 tee_position = glm::vec3(0.0f);
    glm::vec3 pin_position = glm::vec3(0.0f);
    std::vector<glm::vec3> control_points;
    float fairway_width = 0.0f;
    std::vector<material_zone> material_zones;
};

struct render_startup_tile {
    std::string title;
    std::string subtitle;
    bool selected = false;
    std::optional<render_hole_preview> preview;
};

struct render_startup_menu {
    startup_menu_screen screen = startup_menu_screen::none;
    std::string title;
    std::string subtitle;
    std::string footer;
    std::vector<render_startup_tile> tiles;
};

// Tile layout in overlay clip space: a single column on the main and confirm
// screens, a three-column grid on the pickers.
glm::vec2 startup_tile_center(startup_menu_screen screen, int index);
glm::vec2 startup_tile_half_size(startup_menu_screen screen);
// Index of the tile under `point` (overlay clip space), or -1.
int startup_tile_at(startup_menu_screen screen, int count, glm::vec2 point);

void draw_startup_menu(overlay_batch& batch, const text_assets& text, const render_startup_menu& menu);
