#include "core/settings_menu.h"

#include "core/menu_controls.h"
#include "game/text_ids.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>

namespace {
// Settings shown at once; BACK is always under them.
constexpr int visible_rows = 6;
constexpr std::size_t search_max_length = 16;

// Letters and digits, upper case: what a search holds and matches.
std::string search_key(const std::string& text) {
    std::string key;
    for (const char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
            key += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    return key;
}

int shown_rows(const int matches) {
    return std::min(matches, visible_rows);
}

// Keeps a selected setting (an index into the matches) in the shown rows.
void scroll_to_selection(settings_view& view, const int matches) {
    const int rows = shown_rows(matches);
    if (view.selection < matches) {
        view.scroll = std::clamp(view.scroll, view.selection - rows + 1, view.selection);
    }
    view.scroll = std::clamp(view.scroll, 0, std::max(0, matches - rows));
}

// The selection a tile stands for: a shown setting, or BACK after them.
int tile_selection(const settings_view& view, const int tile, const int matches) {
    return tile < shown_rows(matches) ? view.scroll + tile : matches;
}
}

std::vector<int> matching_settings(const std::vector<std::string>& labels, const std::string& search) {
    const std::string key = search_key(search);
    std::vector<int> matches;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (search_key(labels[i]).find(key) != std::string::npos) {
            matches.push_back(static_cast<int>(i));
        }
    }
    return matches;
}

settings_menu_result update_settings_menu(settings_view& view,
                                          settings_values& values,
                                          const std::vector<setting_definition>& definitions,
                                          const std::vector<std::string>& labels,
                                          const input_state& input,
                                          const menu_pointer& pointer,
                                          std::vector<ui_sound>& sounds) {
    settings_menu_result result;
    if (input.escape.pressed) {
        sounds.push_back(ui_sound::back);
        result.back = true;
        return result;
    }
    const std::string typed = search_key(input.text_typed);
    if (!typed.empty() || (input.backspace.pressed && !view.search.empty())) {
        view.search = input.backspace.pressed ? view.search.substr(0, view.search.size() - 1)
                                              : (view.search + typed).substr(0, search_max_length);
        view.selection = 0;
        view.scroll = 0;
    }

    const std::vector<int> matches = matching_settings(labels, view.search);
    const int count = static_cast<int>(matches.size());
    const int previous = view.selection;
    view.selection = std::min(view.selection, count);
    if (input.up.pressed) {
        view.selection = (view.selection + count) % (count + 1);
    }
    if (input.down.pressed) {
        view.selection = (view.selection + 1) % (count + 1);
    }
    scroll_to_selection(view, count);
    if (input.wheel_steps != 0) {
        view.scroll = std::clamp(view.scroll - input.wheel_steps, 0, std::max(0, count - shown_rows(count)));
        if (view.selection < count) {
            view.selection = std::clamp(view.selection, view.scroll, view.scroll + shown_rows(count) - 1);
        }
    }

    const int tiles = shown_rows(count) + 1;
    const int hovered = pointer.hover ? startup_tile_at(startup_menu_screen::main, tiles, *pointer.hover) : -1;
    if (hovered >= 0) {
        view.selection = tile_selection(view, hovered, count);
    }
    const int hit = pointer.click ? startup_tile_at(startup_menu_screen::main, tiles, *pointer.click) : -1;
    if (hit >= 0) {
        view.selection = tile_selection(view, hit, count);
    }
    if (view.selection != previous) {
        sounds.push_back(ui_sound::move);
    }

    if (view.selection == count) {
        if (input.enter.pressed || input.space.pressed || hit >= 0) {
            sounds.push_back(ui_sound::back);
            result.back = true;
        }
        return result;
    }

    const setting_definition& definition = definitions[static_cast<std::size_t>(matches[static_cast<std::size_t>(view.selection)])];
    settings_values adjusted = adjust_setting(values, definition, (input.right.pressed ? 1 : 0) - (input.left.pressed ? 1 : 0));
    // A click steps the way of the arrow on its side of the value; a list of
    // values (on and off) goes round, so a click always switches it.
    if (hit >= 0) {
        const int steps = pointer.click->x >= startup_tile_center(startup_menu_screen::main, hit, tiles).x ? 1 : -1;
        adjusted = definition.value_keys.empty() ? adjust_setting(adjusted, definition, steps) : cycle_setting(adjusted, definition, steps);
    }
    if (setting_value(adjusted, definition) != setting_value(values, definition)) {
        values = adjusted;
        sounds.push_back(ui_sound::move);
        result.changed = true;
    }
    return result;
}

render_startup_menu make_settings_menu_render_data(const settings_view& view,
                                                   const settings_values& values,
                                                   const std::vector<setting_definition>& definitions,
                                                   const text_assets& text) {
    render_startup_menu menu;
    menu.screen = startup_menu_screen::main;
    menu.title = lookup_text(text, text_menu_settings_title);
    menu.subtitle = lookup_text(text, text_menu_settings_subtitle);
    menu.footer = lookup_text(text, text_menu_settings_footer);
    menu.message = view.search.empty() ? lookup_text(text, text_menu_settings_search_hint)
                                       : format_text(text, text_menu_settings_search, {{"search", view.search}});

    std::vector<std::string> labels;
    for (const setting_definition& definition : definitions) {
        labels.push_back(lookup_text(text, definition.label_key.c_str()));
    }
    const std::vector<int> matches = matching_settings(labels, view.search);
    const int count = static_cast<int>(matches.size());
    const int rows = shown_rows(count);
    for (int row = 0; row < rows; ++row) {
        const int index = view.scroll + row;
        const setting_definition& definition = definitions[static_cast<std::size_t>(matches[static_cast<std::size_t>(index)])];
        const std::string value =
            definition.value_keys.empty()
                ? format_text(text, definition.value_key.c_str(), {{"value", std::to_string(setting_value(values, definition))}})
                : lookup_text(text, definition.value_keys[static_cast<std::size_t>(setting_step_index(values, definition))].c_str());
        add_tile(menu, labels[static_cast<std::size_t>(matches[static_cast<std::size_t>(index)])], value, index == view.selection);
    }
    add_tile(menu, lookup_text(text, text_menu_settings_back), lookup_text(text, text_menu_settings_back_hint),
             view.selection >= count);
    menu.more_above = view.scroll > 0;
    menu.more_below = view.scroll + rows < count;
    return menu;
}
