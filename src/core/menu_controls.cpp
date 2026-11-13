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
                      const menu_pointer& pointer,
                      std::vector<ui_sound>& sounds) {
    if (count <= 0) {
        selection = 0;
        return -1;
    }
    // The tiles can change under the selection (signing out drops two).
    selection = std::min(selection, count - 1);
    const int previous = selection;
    const int hovered = pointer.hover ? startup_tile_at(screen, count, *pointer.hover) : -1;
    if (hovered >= 0) {
        selection = hovered;
    }
    const int hit = pointer.click ? startup_tile_at(screen, count, *pointer.click) : -1;
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

int select_course(int& selection,
                  int& top_row,
                  const int count,
                  const input_state& input,
                  const menu_pointer& pointer,
                  std::vector<ui_sound>& sounds, const startup_menu_screen screen) {
    if (count <= 0) {
        selection = 0;
        top_row = 0;
        return -1;
    }
    const int rows = (count + picker_columns - 1) / picker_columns;
    const int last_top = std::max(0, rows - picker_rows);
    selection = std::clamp(selection, 0, count - 1);
    const int previous = selection;
    if (input.wheel_steps != 0) {
        top_row = std::clamp(top_row - input.wheel_steps, 0, last_top);
        const int row = std::clamp(selection / picker_columns, top_row, top_row + picker_rows - 1);
        selection = std::min(row * picker_columns + selection % picker_columns, count - 1);
    }
    if (input.left.pressed) {
        selection = wrap(selection - 1, count);
    }
    if (input.right.pressed) {
        selection = wrap(selection + 1, count);
    }
    if (input.up.pressed) {
        selection = wrap(selection - picker_columns, count);
    }
    if (input.down.pressed) {
        selection = wrap(selection + picker_columns, count);
    }
    const int row = selection / picker_columns;
    top_row = std::clamp(std::clamp(top_row, row - picker_rows + 1, row), 0, last_top);
    const int first = top_row * picker_columns;
    const int shown = std::min(count - first, picker_columns * picker_rows);
    const int hovered = pointer.hover ? startup_tile_at(screen, shown, *pointer.hover) : -1;
    if (hovered >= 0) {
        selection = first + hovered;
    }
    const int hit = pointer.click ? startup_tile_at(screen, shown, *pointer.click) : -1;
    if (hit >= 0) {
        selection = first + hit;
    }
    if (selection != previous) {
        sounds.push_back(ui_sound::move);
    }
    return hit >= 0 ? first + hit : -1;
}

void add_tile(render_startup_menu& menu, std::string title, std::string subtitle, const bool selected) {
    render_startup_tile tile;
    tile.title = std::move(title);
    tile.subtitle = std::move(subtitle);
    tile.selected = selected;
    menu.tiles.push_back(std::move(tile));
}

void add_course_tiles(render_startup_menu& menu, const startup_catalog& catalog, const text_assets& text, const int selection,
                      const int top_row) {
    const std::size_t first = static_cast<std::size_t>(std::max(0, top_row) * picker_columns);
    const std::size_t end = std::min(catalog.courses.size(), first + static_cast<std::size_t>(picker_columns * picker_rows));
    menu.more_above = first > 0;
    menu.more_below = end < catalog.courses.size();
    for (std::size_t i = first; i < end; ++i) {
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
    state.picker_top_row = 0;
    state.message_key.clear();
    state.field = text_input_state{};
    state.waiting = false;
}
