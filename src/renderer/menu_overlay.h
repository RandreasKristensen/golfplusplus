#pragma once

// Startup menus (main, help, hole/course pickers, the online forms, confirm)
// drawn into the overlay batch. GL-free. The tile layout functions are
// shared with the menu flow's mouse hit test (core/startup_flow) so the two
// can't drift apart.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "game/lighting_loader.h"
#include "game/message_severity.h"
#include "game/text_assets.h"
#include "physics/material_zone.h"
#include "renderer/overlay_batch.h"
#include "renderer/text_input.h"

enum class startup_menu_screen {
    none,
    main,
    help,
    course_picker,
    tee_book,    // the times of one course: a grid of tiles
    course_book,  // the courses to choose from: a column of wide cards
    form,    // a message, a text field or a code, then a column of tiles
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
    // A day's heading, drawn without a tile and never selected.
    bool heading = false;
    // With a preview on a course_book card: line one's right side (the title
    // is its left) over the subtitle, line two.
    std::string figures;
    std::optional<render_hole_preview> preview;
};

struct render_startup_menu {
    startup_menu_screen screen = startup_menu_screen::none;
    std::string title;
    std::string subtitle;
    std::string footer;
    // The main menu's bottom corners: the course data's credits and the
    // game's version.
    std::string credits;
    std::string version;
    // A status line under the subtitle; on the pickers it replaces the subtitle.
    // Set text and severity together (set_message).
    std::string message;
    message_severity severity = message_severity::status;
    // Form screens: a text field, or a code shown large.
    std::optional<text_input_state> field;
    std::string code;
    float cursor_time = 0.0f;  // the field's cursor blinks with it
    std::vector<render_startup_tile> tiles;
    // A list that scrolls: more of it above the first tile, or below the
    // last but one (the last is BACK); on a picker above or below its grid
    // of tiles. Shown by arrows beside them.
    bool more_above = false;
    bool more_below = false;
};

// Replaces the status line: its text and its severity, never one without the other.
inline void set_message(render_startup_menu& menu, std::string text, const message_severity severity) {
    menu.message = std::move(text);
    menu.severity = severity;
}

// Tile layout in overlay clip space for `count` tiles: a single column on
// the main, form and confirm screens (closer together when there are many),
// a grid of three by three in a box on the pickers, a column of four cards on
// the course book.
int startup_picker_columns(startup_menu_screen screen);
int startup_picker_rows(startup_menu_screen screen);
glm::vec2 startup_tile_center(startup_menu_screen screen, int index, int count);
glm::vec2 startup_tile_half_size(startup_menu_screen screen, int count);
// Index of the tile under `point` (overlay clip space), or -1.
int startup_tile_at(startup_menu_screen screen, int count, glm::vec2 point);

// Hole thumbnails are drawn in the `ground` colours.
void draw_startup_menu(overlay_batch& batch, const text_assets& text, const render_startup_menu& menu,
                       const ground_colors& ground);
// The title card shown while the game loads, before there is a course to draw.
void draw_loading_screen(overlay_batch& batch, const text_assets& text);
