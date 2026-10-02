#include "core/startup_flow.h"

#include "game/course_loader.h"
#include "game/hole_loader.h"
#include "game/json_util.h"
#include "game/text_ids.h"

#include <array>
#include <filesystem>
#include <system_error>
#include <utility>

namespace {
constexpr int main_menu_item_count = static_cast<int>(main_menu_item::count);
constexpr int confirm_item_count = 2;
constexpr int picker_columns = 3;

render_hole_preview make_hole_preview(const hole_data& hole) {
    render_hole_preview preview;
    preview.tee_position = hole.tee_position;
    preview.pin_position = hole.pin_position;
    preview.control_points = hole.spline.control_points;
    preview.fairway_width = hole.spline.width;
    preview.material_zones = hole.material_zones;
    return preview;
}

startup_course_option make_course_option(const std::string& asset_root, const course_definition& course) {
    startup_course_option option;
    option.course = course;
    for (std::size_t i = 0; i < course.holes.size(); ++i) {
        const std::optional<hole_data> hole = load_hole_from_file(course_hole_path(asset_root, course, i));
        if (!hole) {
            continue;
        }
        option.total_par += hole->par;
        if (i == 0) {
            option.preview = make_hole_preview(*hole);
        }
    }
    return option;
}

startup_menu_screen screen_for_flow(const startup_flow flow) {
    switch (flow) {
    case startup_flow::main:
        return startup_menu_screen::main;
    case startup_flow::help:
        return startup_menu_screen::help;
    case startup_flow::hole_picker:
        return startup_menu_screen::hole_picker;
    case startup_flow::course_picker:
        return startup_menu_screen::course_picker;
    case startup_flow::playing:
        return startup_menu_screen::none;
    }
    return startup_menu_screen::none;
}

int item_count(const startup_flow flow, const startup_catalog& catalog) {
    switch (flow) {
    case startup_flow::main:
        return main_menu_item_count;
    case startup_flow::hole_picker:
        return static_cast<int>(catalog.holes.size());
    case startup_flow::course_picker:
        return static_cast<int>(catalog.courses.size());
    case startup_flow::help:
    case startup_flow::playing:
        return 0;
    }
    return 0;
}

int wrap(const int value, const int count) {
    return ((value % count) + count) % count;
}

// Moves `selection` by arrow keys (left/right by one, up/down by a row) or
// onto a clicked tile; queues a move sound when it changed. Returns the
// clicked tile, or -1.
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

course_definition practice_course(const startup_hole_option& option) {
    course_definition course;
    course.id = "practice_" + option.hole.id;
    course.name = option.hole.name;
    course.holes = {option.path};
    course.backdrop = option.backdrop;
    course.practice = true;
    return course;
}

void add_tile(render_startup_menu& menu, std::string title, std::string subtitle, const bool selected) {
    render_startup_tile tile;
    tile.title = std::move(title);
    tile.subtitle = std::move(subtitle);
    tile.selected = selected;
    menu.tiles.push_back(std::move(tile));
}

void open_screen(startup_flow_state& state, const startup_flow flow) {
    state.flow = flow;
    state.selection = 0;
}

// The backdrop of the first course that plays the hole at `path`, empty when none does.
std::string backdrop_for_hole(const game_content& content, const std::filesystem::path& path) {
    for (const course_definition& course : content.courses) {
        for (std::size_t i = 0; i < course.holes.size(); ++i) {
            std::error_code error;
            if (std::filesystem::equivalent(course_hole_path(content.asset_root, course, i), path, error)) {
                return course.backdrop;
            }
        }
    }
    return {};
}
}

startup_catalog load_startup_catalog(const game_content& content) {
    startup_catalog catalog;
    const std::filesystem::path root(content.asset_root);
    for (const std::filesystem::path& path : json_files_in_directory(root / "holes")) {
        std::optional<hole_data> hole = load_hole_from_file(path.string());
        if (!hole) {
            continue;
        }
        if (hole->id.empty()) {
            hole->id = path.stem().string();
        }
        if (hole->name.empty()) {
            hole->name = hole->id;
        }
        std::error_code error;
        const std::filesystem::path relative = std::filesystem::relative(path, root, error);
        catalog.holes.push_back(startup_hole_option{error ? path.string() : relative.generic_string(), std::move(*hole),
                                                    backdrop_for_hole(content, path)});
    }
    for (const course_definition& course : content.courses) {
        catalog.courses.push_back(make_course_option(content.asset_root, course));
    }
    return catalog;
}

startup_menu_result update_startup_menu(startup_flow_state& state,
                                        const input_state& input,
                                        const std::optional<glm::vec2> click,
                                        const startup_catalog& catalog) {
    startup_menu_result result;
    const int count = item_count(state.flow, catalog);
    const int columns = state.flow == startup_flow::main ? 1 : picker_columns;
    const int hit = select_with_input(screen_for_flow(state.flow), columns, state.selection, count, input, click, result.sounds);

    if (input.backspace.pressed || input.escape.pressed) {
        result.sounds.push_back(ui_sound::back);
        if (state.flow == startup_flow::main) {
            result.action = startup_action::quit;
        } else {
            return_to_main_menu(state);
        }
        return result;
    }
    if (!(input.enter.pressed || input.space.pressed || hit >= 0) || count <= 0) {
        return result;
    }

    result.sounds.push_back(ui_sound::select);
    const std::size_t selected = static_cast<std::size_t>(state.selection);
    switch (state.flow) {
    case startup_flow::main:
        switch (static_cast<main_menu_item>(state.selection)) {
        case main_menu_item::play_hole:
            open_screen(state, startup_flow::hole_picker);
            break;
        case main_menu_item::play_course:
            open_screen(state, startup_flow::course_picker);
            break;
        case main_menu_item::help:
            open_screen(state, startup_flow::help);
            break;
        case main_menu_item::quit:
        case main_menu_item::count:
            result.action = startup_action::quit;
            break;
        }
        break;
    case startup_flow::hole_picker:
        result.action = startup_action::start_course;
        result.course = practice_course(catalog.holes[selected]);
        break;
    case startup_flow::course_picker:
        result.action = startup_action::start_course;
        result.course = catalog.courses[selected].course;
        break;
    case startup_flow::help:
    case startup_flow::playing:
        break;
    }
    return result;
}

void enter_playing(startup_flow_state& state) {
    state.flow = startup_flow::playing;
}

void return_to_main_menu(startup_flow_state& state) {
    state = startup_flow_state{};
}

void open_confirm_menu(startup_flow_state& state) {
    state.confirm_active = true;
    state.confirm_selection = 1;
}

confirm_menu_result update_confirm_menu(startup_flow_state& state,
                                        const input_state& input,
                                        const std::optional<glm::vec2> click) {
    confirm_menu_result result;
    const int hit = select_with_input(startup_menu_screen::confirm, 1, state.confirm_selection, confirm_item_count,
                                      input, click, result.sounds);

    if (input.enter.pressed || input.space.pressed || hit >= 0) {
        result.sounds.push_back(ui_sound::select);
        state.confirm_active = false;
        result.leave_round = state.confirm_selection == 0;
    } else if (input.escape.pressed || input.backspace.pressed) {
        result.sounds.push_back(ui_sound::back);
        state.confirm_active = false;
    }
    return result;
}

render_startup_menu make_startup_menu_render_data(const startup_flow_state& state,
                                                  const startup_catalog& catalog,
                                                  const text_assets& text) {
    render_startup_menu menu;
    menu.screen = screen_for_flow(state.flow);
    const auto selected = [&state](const std::size_t i) { return static_cast<int>(i) == state.selection; };

    switch (state.flow) {
    case startup_flow::main: {
        menu.title = lookup_text(text, text_menu_main_title);
        menu.subtitle = lookup_text(text, text_menu_main_subtitle);
        menu.footer = lookup_text(text, text_menu_main_footer);
        const std::array<std::pair<const char*, const char*>, main_menu_item_count> items{{
            {text_menu_main_play_hole, text_menu_main_play_hole_hint},
            {text_menu_main_play_course, text_menu_main_play_course_hint},
            {text_menu_main_help, text_menu_main_help_hint},
            {text_menu_main_quit, text_menu_main_quit_hint},
        }};
        for (std::size_t i = 0; i < items.size(); ++i) {
            add_tile(menu, lookup_text(text, items[i].first), lookup_text(text, items[i].second), selected(i));
        }
        break;
    }
    case startup_flow::help:
        menu.title = lookup_text(text, text_menu_help_title);
        menu.subtitle = lookup_text(text, text_menu_help_subtitle);
        menu.footer = lookup_text(text, text_menu_help_footer);
        break;
    case startup_flow::hole_picker:
        menu.title = lookup_text(text, text_menu_hole_picker_title);
        menu.subtitle = lookup_text(text, text_menu_hole_picker_subtitle);
        menu.footer = lookup_text(text, text_menu_hole_picker_footer);
        for (std::size_t i = 0; i < catalog.holes.size(); ++i) {
            const hole_data& hole = catalog.holes[i].hole;
            add_tile(menu, hole.name, format_text(text, text_menu_hole_picker_tile, {{"par", std::to_string(hole.par)}}), selected(i));
            menu.tiles.back().preview = make_hole_preview(hole);
        }
        break;
    case startup_flow::course_picker:
        menu.title = lookup_text(text, text_menu_course_picker_title);
        menu.subtitle = lookup_text(text, text_menu_course_picker_subtitle);
        menu.footer = lookup_text(text, text_menu_course_picker_footer);
        for (std::size_t i = 0; i < catalog.courses.size(); ++i) {
            const startup_course_option& option = catalog.courses[i];
            add_tile(menu,
                     option.course.name,
                     format_text(text,
                                 text_menu_course_picker_tile,
                                 {{"holes", std::to_string(option.course.holes.size())},
                                  {"par", std::to_string(option.total_par)}}),
                     selected(i));
            menu.tiles.back().preview = option.preview;
        }
        break;
    case startup_flow::playing:
        break;
    }
    return menu;
}

render_startup_menu make_confirm_menu_render_data(const startup_flow_state& state, const text_assets& text) {
    render_startup_menu menu;
    menu.screen = startup_menu_screen::confirm;
    menu.title = lookup_text(text, text_menu_confirm_title);
    add_tile(menu, lookup_text(text, text_menu_confirm_yes), "", state.confirm_selection == 0);
    add_tile(menu, lookup_text(text, text_menu_confirm_no), "", state.confirm_selection == 1);
    return menu;
}
