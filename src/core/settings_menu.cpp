#include "core/settings_menu.h"

#include "core/menu_controls.h"
#include "game/text_ids.h"

#include <cstddef>
#include <string>

settings_menu_result update_settings_menu(int& selection,
                                          settings_values& values,
                                          const std::vector<setting_definition>& definitions,
                                          const input_state& input,
                                          const std::optional<glm::vec2> click,
                                          std::vector<ui_sound>& sounds) {
    settings_menu_result result;
    const int count = static_cast<int>(definitions.size()) + 1;
    // Left and right change the setting, so only up, down and clicks move.
    input_state moves = input;
    moves.left = {};
    moves.right = {};
    const int hit = select_with_input(startup_menu_screen::main, 1, selection, count, moves, click, sounds);

    if (input.escape.pressed || input.backspace.pressed) {
        sounds.push_back(ui_sound::back);
        result.back = true;
        return result;
    }
    const bool on_back = selection == count - 1;
    if (on_back) {
        if (input.enter.pressed || input.space.pressed || hit >= 0) {
            sounds.push_back(ui_sound::back);
            result.back = true;
        }
        return result;
    }

    const int steps = (input.right.pressed ? 1 : 0) - (input.left.pressed ? 1 : 0);
    const setting_definition& definition = definitions[static_cast<std::size_t>(selection)];
    const settings_values adjusted = adjust_setting(values, definition, steps);
    if (setting_value(adjusted, definition) != setting_value(values, definition)) {
        values = adjusted;
        sounds.push_back(ui_sound::move);
        result.changed = true;
    }
    return result;
}

render_startup_menu make_settings_menu_render_data(const int selection,
                                                   const settings_values& values,
                                                   const std::vector<setting_definition>& definitions,
                                                   const text_assets& text) {
    render_startup_menu menu;
    menu.screen = startup_menu_screen::main;
    menu.title = lookup_text(text, text_menu_settings_title);
    menu.subtitle = lookup_text(text, text_menu_settings_subtitle);
    menu.footer = lookup_text(text, text_menu_settings_footer);
    for (std::size_t i = 0; i < definitions.size(); ++i) {
        const setting_definition& definition = definitions[i];
        add_tile(menu, lookup_text(text, definition.label_key.c_str()),
                 format_text(text, definition.value_key.c_str(), {{"value", std::to_string(setting_value(values, definition))}}),
                 static_cast<int>(i) == selection);
    }
    add_tile(menu, lookup_text(text, text_menu_settings_back), lookup_text(text, text_menu_settings_back_hint),
             selection == static_cast<int>(definitions.size()));
    return menu;
}
