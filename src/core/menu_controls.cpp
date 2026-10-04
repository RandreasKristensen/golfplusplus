#include "core/menu_controls.h"

#include "game/text_ids.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace {
int wrap(const int value, const int count) {
    return ((value % count) + count) % count;
}
}

int select_with_input(const startup_menu_screen screen,
                      const int columns,
                      int& selection,
                      const int count,
                      const input_state& input,
                      const std::optional<glm::vec2> click,
                      std::vector<ui_sound>& sounds) {
    if (count <= 0) {
        selection = 0;
        return -1;
    }
    // The tiles can change under the selection (signing out drops two).
    selection = std::min(selection, count - 1);
    const int previous = selection;
    const int hit = click ? startup_tile_at(screen, count, *click) : -1;
    if (hit >= 0) {
        selection = hit;
    }
    if (input.left.pressed) {
        selection = wrap(selection - 1, count);
    }
    if (input.right.pressed) {
        selection = wrap(selection + 1, count);
    }
    if (input.up.pressed) {
        selection = wrap(selection - columns, count);
    }
    if (input.down.pressed) {
        selection = wrap(selection + columns, count);
    }
    if (selection != previous) {
        sounds.push_back(ui_sound::move);
    }
    return hit;
}

void add_tile(render_startup_menu& menu, std::string title, std::string subtitle, const bool selected) {
    render_startup_tile tile;
    tile.title = std::move(title);
    tile.subtitle = std::move(subtitle);
    tile.selected = selected;
    menu.tiles.push_back(std::move(tile));
}

void add_course_tiles(render_startup_menu& menu, const startup_catalog& catalog, const text_assets& text, const int selection) {
    for (std::size_t i = 0; i < catalog.courses.size(); ++i) {
        const startup_course_option& option = catalog.courses[i];
        add_tile(menu,
                 option.course.name,
                 format_text(text,
                             text_menu_course_picker_tile,
                             {{"holes", std::to_string(option.course.holes.size())}, {"par", std::to_string(option.total_par)}}),
                 static_cast<int>(i) == selection);
        menu.tiles.back().preview = option.preview;
    }
}

void open_screen(startup_flow_state& state, const startup_flow flow) {
    state.flow = flow;
    state.selection = 0;
    state.message_key.clear();
    state.field = text_input_state{};
    state.waiting = false;
}
