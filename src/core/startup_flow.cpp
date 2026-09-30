#include "core/startup_flow.h"

#include "game/course_loader.h"
#include "game/hole_loader.h"
#include "game/text_ids.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <utility>

namespace {
constexpr int main_menu_item_count = 4;
constexpr int confirm_item_count = 2;

std::string relative_asset_path(const std::filesystem::path& asset_root, const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(path, asset_root, error);
    if (error) {
        return path.string();
    }
    return relative.generic_string();
}

render_hole_preview make_hole_preview(const hole_data& hole) {
    render_hole_preview preview;
    preview.tee_position = hole.tee_position;
    preview.pin_position = hole.pin_position;
    preview.control_points = hole.spline.control_points;
    preview.fairway_width = hole.spline.width;
    preview.material_zones = hole.material_zones;
    return preview;
}

int course_total_par(const course_definition& course, const std::string& asset_root) {
    int total = 0;
    for (std::size_t i = 0; i < course.holes.size(); ++i) {
        const std::optional<hole_data> hole = load_hole_from_file(course_hole_path(asset_root, course, i));
        if (hole) {
            total += std::max(1, hole->par);
        }
    }
    return total;
}

std::optional<render_hole_preview> course_preview(const course_definition& course, const std::string& asset_root) {
    if (course.holes.empty()) {
        return std::nullopt;
    }
    const std::optional<hole_data> hole = load_hole_from_file(course_hole_path(asset_root, course, 0));
    if (!hole) {
        return std::nullopt;
    }
    return make_hole_preview(*hole);
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
    default:
        return startup_menu_screen::none;
    }
}

int hit_index(const startup_menu_screen screen, const int count, const std::optional<glm::vec2> click) {
    if (!click || count <= 0) {
        return -1;
    }
    return startup_tile_at(screen, count, *click);
}

int item_count(const startup_flow flow,
               const std::vector<startup_hole_option>& holes,
               const std::vector<course_definition>& courses) {
    switch (flow) {
    case startup_flow::main:
        return main_menu_item_count;
    case startup_flow::hole_picker:
        return static_cast<int>(holes.size());
    case startup_flow::course_picker:
        return static_cast<int>(courses.size());
    default:
        return 0;
    }
}

void move_selection(const int columns, int& selection, const int count, const input_state& input) {
    if (count <= 0) {
        selection = 0;
        return;
    }

    if (input.left.pressed) {
        selection = (selection + count - 1) % count;
    }
    if (input.right.pressed) {
        selection = (selection + 1) % count;
    }
    if (input.up.pressed) {
        selection = (selection + count - columns) % count;
    }
    if (input.down.pressed) {
        selection = (selection + columns) % count;
    }
    selection = std::max(0, std::min(selection, count - 1));
}

// Clicks select a tile and move the keyboard selection onto it.
int select_with_input(const startup_menu_screen screen,
                      const int columns,
                      int& selection,
                      const int count,
                      const input_state& input,
                      const std::optional<glm::vec2> click,
                      std::vector<ui_sound>& sounds) {
    const int previous_selection = selection;
    const int hit = hit_index(screen, count, click);
    if (hit >= 0) {
        selection = hit;
    }
    move_selection(columns, selection, count, input);
    if (selection != previous_selection) {
        sounds.push_back(ui_sound::move);
    }
    return hit;
}

course_definition single_hole_course(const startup_hole_option& option) {
    course_definition course;
    course.id = option.hole.id.empty() ? "single_hole" : "single_" + option.hole.id;
    course.name = option.hole.name.empty() ? option.hole.id : option.hole.name;
    course.hole_count = 1;
    course.holes = {option.path};
    return course;
}

void add_tile(render_startup_menu& menu, std::string title, std::string subtitle, const bool selected) {
    render_startup_tile tile;
    tile.title = std::move(title);
    tile.subtitle = std::move(subtitle);
    tile.selected = selected;
    menu.tiles.push_back(std::move(tile));
}
}

std::vector<startup_hole_option> load_startup_holes(const std::string& asset_root) {
    std::vector<startup_hole_option> options;
    const std::filesystem::path root(asset_root);
    const std::filesystem::path holes_dir = root / "holes";
    if (!std::filesystem::exists(holes_dir) || !std::filesystem::is_directory(holes_dir)) {
        return options;
    }

    std::vector<std::filesystem::path> files;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(holes_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());
    for (const std::filesystem::path& path : files) {
        std::optional<hole_data> hole = load_hole_from_file(path.string());
        if (!hole) {
            continue;
        }
        if (hole->id.empty()) {
            hole->id = path.stem().string();
        }
        startup_hole_option option;
        option.path = relative_asset_path(root, path);
        option.hole = *hole;
        options.push_back(option);
    }
    return options;
}

startup_menu_result update_startup_menu(startup_flow_state& state,
                                        const input_state& input,
                                        const std::optional<glm::vec2> click,
                                        const std::vector<startup_hole_option>& holes,
                                        const game_content& content) {
    startup_menu_result result;
    const int count = item_count(state.flow, holes, content.courses);
    const int columns = state.flow == startup_flow::main ? 1 : 3;
    const int hit = select_with_input(screen_for_flow(state.flow), columns, state.selection, count, input, click, result.sounds);

    const bool accept = input.enter.pressed || input.space.pressed || hit >= 0;
    const bool back = input.backspace.pressed || input.escape.pressed;
    if (back) {
        result.sounds.push_back(ui_sound::back);
        if (state.flow == startup_flow::main) {
            result.action = startup_action::quit;
        } else {
            return_to_main_menu(state);
        }
        return result;
    }
    if (!accept) {
        return result;
    }

    if (state.flow == startup_flow::main) {
        result.sounds.push_back(ui_sound::select);
        if (state.selection == 0) {
            state.flow = startup_flow::hole_picker;
            state.selection = 0;
        } else if (state.selection == 1) {
            state.flow = startup_flow::course_picker;
            state.selection = 0;
        } else if (state.selection == 2) {
            state.flow = startup_flow::help;
            state.selection = 0;
        } else {
            result.action = startup_action::quit;
        }
    } else if (state.flow == startup_flow::hole_picker && count > 0) {
        result.sounds.push_back(ui_sound::select);
        result.action = startup_action::start_course;
        result.course = single_hole_course(holes[static_cast<std::size_t>(state.selection)]);
    } else if (state.flow == startup_flow::course_picker && count > 0) {
        result.sounds.push_back(ui_sound::select);
        result.action = startup_action::start_course;
        result.course = content.courses[static_cast<std::size_t>(state.selection)];
    }
    return result;
}

void enter_playing(startup_flow_state& state) {
    state.flow = startup_flow::playing;
}

void return_to_main_menu(startup_flow_state& state) {
    state.flow = startup_flow::main;
    state.selection = 0;
    state.confirm_active = false;
    state.confirm_selection = 1;
}

void open_confirm_menu(startup_flow_state& state) {
    state.confirm_active = true;
    state.confirm_selection = 1;
}

confirm_menu_result update_confirm_menu(startup_flow_state& state,
                                        const input_state& input,
                                        const std::optional<glm::vec2> click) {
    confirm_menu_result result;
    const int hit = select_with_input(startup_menu_screen::main,
                                      1,
                                      state.confirm_selection,
                                      confirm_item_count,
                                      input,
                                      click,
                                      result.sounds);

    const bool accept = input.enter.pressed || input.space.pressed || hit >= 0;
    const bool cancel = input.escape.pressed || input.backspace.pressed;
    if (accept) {
        result.sounds.push_back(ui_sound::select);
        state.confirm_active = false;
        result.leave_round = state.confirm_selection == 0;
    } else if (cancel) {
        result.sounds.push_back(ui_sound::back);
        state.confirm_active = false;
    }
    return result;
}

render_startup_menu make_startup_menu_render_data(const startup_flow_state& state,
                                                  const std::vector<startup_hole_option>& holes,
                                                  const game_content& content,
                                                  const text_assets& text) {
    render_startup_menu menu;
    menu.screen = screen_for_flow(state.flow);
    if (menu.screen == startup_menu_screen::none) {
        return menu;
    }

    if (state.flow == startup_flow::main) {
        menu.title = lookup_text(text, text_menu_main_title);
        menu.subtitle = lookup_text(text, text_menu_main_subtitle);
        menu.footer = lookup_text(text, text_menu_main_footer);
        const std::array<std::pair<const char*, const char*>, main_menu_item_count> items{{
            {text_menu_main_play_hole, text_menu_main_play_hole_hint},
            {text_menu_main_play_course, text_menu_main_play_course_hint},
            {text_menu_main_help, text_menu_main_help_hint},
            {text_menu_main_quit, text_menu_main_quit_hint}
        }};
        for (std::size_t i = 0; i < items.size(); ++i) {
            add_tile(menu,
                     lookup_text(text, items[i].first),
                     lookup_text(text, items[i].second),
                     static_cast<int>(i) == state.selection);
        }
        return menu;
    }

    if (state.flow == startup_flow::help) {
        menu.title = lookup_text(text, text_menu_help_title);
        menu.subtitle = lookup_text(text, text_menu_help_subtitle);
        menu.footer = lookup_text(text, text_menu_help_footer);
        return menu;
    }

    if (state.flow == startup_flow::hole_picker) {
        menu.title = lookup_text(text, text_menu_hole_picker_title);
        menu.subtitle = lookup_text(text, text_menu_hole_picker_subtitle);
        menu.footer = lookup_text(text, text_menu_hole_picker_footer);
        for (std::size_t i = 0; i < holes.size(); ++i) {
            const hole_data& hole = holes[i].hole;
            add_tile(menu,
                     hole.name.empty() ? hole.id : hole.name,
                     format_text(text, text_menu_hole_picker_tile, {{"par", std::to_string(std::max(1, hole.par))}}),
                     static_cast<int>(i) == state.selection);
            menu.tiles.back().has_preview = true;
            menu.tiles.back().preview = make_hole_preview(hole);
        }
        return menu;
    }

    menu.title = lookup_text(text, text_menu_course_picker_title);
    menu.subtitle = lookup_text(text, text_menu_course_picker_subtitle);
    menu.footer = lookup_text(text, text_menu_course_picker_footer);
    for (std::size_t i = 0; i < content.courses.size(); ++i) {
        const course_definition& course = content.courses[i];
        add_tile(menu,
                 course.name.empty() ? course.id : course.name,
                 format_text(text,
                             text_menu_course_picker_tile,
                             {{"holes", std::to_string(std::max(0, course.hole_count))},
                              {"par", std::to_string(std::max(0, course_total_par(course, content.asset_root)))}}),
                 static_cast<int>(i) == state.selection);
        const std::optional<render_hole_preview> preview = course_preview(course, content.asset_root);
        if (preview) {
            menu.tiles.back().has_preview = true;
            menu.tiles.back().preview = *preview;
        }
    }
    return menu;
}

render_startup_menu make_confirm_menu_render_data(const startup_flow_state& state, const text_assets& text) {
    render_startup_menu menu;
    menu.screen = startup_menu_screen::main;
    menu.title = lookup_text(text, text_menu_confirm_title);
    add_tile(menu, lookup_text(text, text_menu_confirm_yes), "", state.confirm_selection == 0);
    add_tile(menu, lookup_text(text, text_menu_confirm_no), "", state.confirm_selection == 1);
    return menu;
}
