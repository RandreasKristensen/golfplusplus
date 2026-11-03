#include "core/look_tuning.h"

#include "game/content_files.h"
#include "game/lighting_numbers.h"
#include "game/text_ids.h"

#include <cmath>
#include <string>

#include <glm/common.hpp>
#include <glm/trigonometric.hpp>

namespace {
const char* file_name(const char* file) {
    return std::string(file) == picture_style_path ? text_look_picture : text_look_lighting;
}

const char* problem_text(const look_problem problem) {
    switch (problem) {
    case look_problem::unreadable:
        return text_look_unreadable;
    case look_problem::not_json:
        return text_look_not_json;
    case look_problem::not_valid:
        break;
    }
    return text_look_not_valid;
}

std::size_t sun_key(const look_tuning_state& state, const lighting_palette& shown) {
    return nearest_lighting_key(shown.keys, state.sun_elevation_deg);
}
}

look_tuning_keys look_tuning_keys_of(const input_state& input) {
    return look_tuning_keys{input.key_f2.pressed, input.up.pressed,   input.down.pressed,
                            input.left.pressed,   input.right.pressed, input.left_shift.is_down || input.right_shift.is_down};
}

int update_look_panel(look_tuning_state& state, const look_tuning_keys& keys, const bool allowed) {
    if (!allowed || state.entries.empty()) {
        state.open = false;
        return 0;
    }
    if (keys.toggle) {
        state.open = !state.open;
    }
    if (!state.open) {
        return 0;
    }
    const std::size_t count = state.entries.size();
    if (keys.down) {
        state.selected = (state.selected + 1) % count;
    }
    if (keys.up) {
        state.selected = (state.selected + count - 1) % count;
    }
    return (keys.right ? 1 : 0) - (keys.left ? 1 : 0);
}

std::optional<look_reload> run_look_tuning(look_tuning_state& state, const look_tuning_keys& keys, const bool allowed,
                                           const lighting_palette& shown, const float dt) {
    const int direction = update_look_panel(state, keys, allowed);
    if (direction != 0) {
        const tuning_entry& entry = state.entries[state.selected];
        const std::optional<std::string> text = read_text_file(state.watch.lighting_file);
        const std::optional<std::string> stepped =
            text ? step_lighting_text(*text, entry, tuning_entry_path(entry, sun_key(state, shown)), direction, keys.fine)
                 : std::nullopt;
        if (stepped) {
            state.unwritten = !replace_text_file(state.watch.lighting_file, *stepped);
            if (!state.unwritten) {
                return reload_look(state.watch);
            }
        }
    }
    return poll_look(state.watch, dt);
}

render_god_menu look_tuning_view(const look_tuning_state& state, const lighting_palette& shown, const text_assets& text) {
    const std::size_t key = sun_key(state, shown);
    const std::string elevation = std::to_string(static_cast<int>(std::lround(shown.keys[key].sun_elevation_deg)));
    render_god_menu view;
    view.title = lookup_text(text, text_look_title);
    for (std::size_t i = 0; i < state.entries.size(); ++i) {
        const tuning_entry& entry = state.entries[i];
        const std::optional<lighting_number> number = lighting_number_at(shown, tuning_entry_path(entry, key));
        render_god_menu_row row;
        row.label = format_text(text, entry.label.c_str(), {{"elevation", elevation}});
        if (number) {
            row.value = json_number_text(number->value, tuning_decimals(entry, number->whole), false);
        }
        row.selected = i == state.selected;
        row.held = true;
        view.rows.push_back(std::move(row));
    }
    view.note = lookup_text(text, text_look_keys);
    return view;
}

std::string look_notice(const look_tuning_state& state, const text_assets& text) {
    if (const std::optional<look_error>& error = state.watch.error) {
        return format_text(text, text_look_failed,
                           {{"file", lookup_text(text, file_name(error->file))}, {"why", lookup_text(text, problem_text(error->problem))}});
    }
    return state.unwritten ? lookup_text(text, text_look_unwritten) : std::string();
}

void show_look_tuning(render_data& data, look_tuning_state& state, const lighting_palette& shown, const text_assets& text) {
    state.sun_elevation_deg = glm::degrees(std::asin(glm::clamp(data.lighting.sun_direction.y, -1.0f, 1.0f)));
    data.look_notice = look_notice(state, text);
    if (state.open) {
        data.god_menu = look_tuning_view(state, shown, text);
    }
}
