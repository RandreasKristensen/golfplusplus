#include "core/startup_flow.h"

#include "core/menu_controls.h"
#include "core/online_menus.h"
#include "core/settings_menu.h"
#include "game/content_files.h"
#include "game/pixel_font_data.h"
#include "game/text_ids.h"

#include <utility>

namespace {
constexpr int confirm_item_count = 3;
constexpr int confirm_settings_item = 2;

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
    case startup_flow::settings:
        return startup_menu_screen::main;
    case startup_flow::help:
        return startup_menu_screen::help;
    case startup_flow::course_picker:
    case startup_flow::online_course_picker:
        return startup_menu_screen::course_picker;
    case startup_flow::online_login:
    case startup_flow::name_entry:
    case startup_flow::link_code_entry:
    case startup_flow::link_code_show:
    case startup_flow::joining:
        return startup_menu_screen::form;
    case startup_flow::playing:
        return startup_menu_screen::none;
    }
    return startup_menu_screen::none;
}

// The main menu's items for how online play is going.
std::vector<main_menu_item> main_menu_items(const online_menu_status& online) {
    std::vector<main_menu_item> items{main_menu_item::play_online, main_menu_item::play_offline};
    if (signed_in(online)) {
        if (!online.guest) {
            items.push_back(main_menu_item::link_login);
        }
        items.push_back(main_menu_item::sign_out);
    }
    items.push_back(main_menu_item::help);
    items.push_back(main_menu_item::settings);
    items.push_back(main_menu_item::quit);
    return items;
}

std::pair<const char*, const char*> main_menu_text(const main_menu_item item) {
    switch (item) {
    case main_menu_item::play_online:
        return {text_menu_main_play_online, text_menu_main_play_online_hint};
    case main_menu_item::play_offline:
        return {text_menu_main_play_offline, text_menu_main_play_offline_hint};
    case main_menu_item::link_login:
        return {text_menu_main_link_login, text_menu_main_link_login_hint};
    case main_menu_item::sign_out:
        return {text_menu_main_sign_out, text_menu_main_sign_out_hint};
    case main_menu_item::help:
        return {text_menu_main_help, text_menu_main_help_hint};
    case main_menu_item::settings:
        return {text_menu_main_settings, text_menu_main_settings_hint};
    case main_menu_item::quit:
        return {text_menu_main_quit, text_menu_main_quit_hint};
    }
    return {text_menu_main_quit, text_menu_main_quit_hint};
}

int item_count(const startup_flow flow, const startup_catalog& catalog, const online_menu_status& online) {
    switch (flow) {
    case startup_flow::main:
        return static_cast<int>(main_menu_items(online).size());
    case startup_flow::course_picker:
        return static_cast<int>(catalog.courses.size());
    default:
        return 0;
    }
}

void accept_main_menu(startup_flow_state& state, const online_menu_status& online, startup_menu_result& result) {
    switch (main_menu_items(online)[static_cast<std::size_t>(state.selection)]) {
    case main_menu_item::play_online:
        open_play_online(state, online, result);
        break;
    case main_menu_item::play_offline:
        open_screen(state, startup_flow::course_picker);
        break;
    case main_menu_item::link_login:
        open_link_code_show(state, online, result);
        break;
    case main_menu_item::sign_out:
        result.requests.push_back(online_request{online_request_type::sign_out, {}});
        break;
    case main_menu_item::help:
        open_screen(state, startup_flow::help);
        break;
    case main_menu_item::settings:
        open_screen(state, startup_flow::settings);
        break;
    case main_menu_item::quit:
        result.action = startup_action::quit;
        break;
    }
}
}

bool signed_in(const online_menu_status& online) {
    return online.status == net_status::connected && online.account_id != 0;
}

startup_catalog load_startup_catalog(const game_content& content, const text_assets& text) {
    startup_catalog catalog;
    for (const course_definition& course : content.courses) {
        catalog.courses.push_back(make_course_option(content.asset_root, course));
    }
    catalog.name_chars = font_charset(text.font);
    catalog.name_max_length = static_cast<std::size_t>(content.tuning.server.name_max_length);
    catalog.settings = content.settings;
    return catalog;
}

startup_menu_result update_startup_menu(startup_flow_state& state,
                                        const input_state& input,
                                        const std::optional<glm::vec2> click,
                                        const startup_catalog& catalog,
                                        const online_menu_status& online) {
    startup_menu_result result;
    state.clock_seconds = online.clock_seconds;
    if (is_online_flow(state.flow)) {
        update_online_menu(state, input, click, catalog, online, result);
        return result;
    }
    if (state.flow == startup_flow::settings) {
        const settings_menu_result settings =
            update_settings_menu(state.selection, state.settings, catalog.settings, input, click, result.sounds);
        result.settings_changed = settings.changed;
        if (settings.back) {
            return_to_main_menu(state);
        }
        return result;
    }

    const int count = item_count(state.flow, catalog, online);
    const bool picker = state.flow == startup_flow::course_picker;
    const int hit = select_with_input(screen_for_flow(state.flow), picker ? picker_columns : 1, state.selection, count, input,
                                      click, result.sounds);

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
        accept_main_menu(state, online, result);
        break;
    case startup_flow::course_picker:
        result.action = startup_action::start_course;
        result.course = catalog.courses[selected].course;
        break;
    default:
        break;
    }
    return result;
}

void enter_playing(startup_flow_state& state) {
    state.flow = startup_flow::playing;
}

void return_to_main_menu(startup_flow_state& state) {
    settings_values settings = std::move(state.settings);
    state = startup_flow_state{};
    state.settings = std::move(settings);
}

void open_online_login(startup_flow_state& state) {
    open_screen(state, startup_flow::online_login);
}

bool wants_text_input(const startup_flow_state& state) {
    return state.flow == startup_flow::name_entry || state.flow == startup_flow::link_code_entry;
}

void open_confirm_menu(startup_flow_state& state) {
    state.confirm_active = true;
    state.confirm_selection = 1;
    state.confirm_settings = false;
}

confirm_menu_result update_confirm_menu(startup_flow_state& state,
                                        const input_state& input,
                                        const std::optional<glm::vec2> click,
                                        const std::vector<setting_definition>& settings) {
    confirm_menu_result result;
    if (state.confirm_settings) {
        const settings_menu_result outcome =
            update_settings_menu(state.settings_selection, state.settings, settings, input, click, result.sounds);
        result.settings_changed = outcome.changed;
        state.confirm_settings = !outcome.back;
        return result;
    }
    const int hit = select_with_input(startup_menu_screen::confirm, 1, state.confirm_selection, confirm_item_count,
                                      input, click, result.sounds);

    if (input.enter.pressed || input.space.pressed || hit >= 0) {
        result.sounds.push_back(ui_sound::select);
        if (state.confirm_selection == confirm_settings_item) {
            state.confirm_settings = true;
            state.settings_selection = 0;
            return result;
        }
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
                                                  const text_assets& text,
                                                  const online_menu_status& online) {
    render_startup_menu menu;
    if (is_online_flow(state.flow)) {
        make_online_menu_render_data(state, catalog, text, online, menu);
        return menu;
    }

    menu.screen = screen_for_flow(state.flow);
    if (!state.message_key.empty()) {
        menu.message = lookup_text(text, state.message_key.c_str());
        menu.message_is_error = true;
    }
    const auto selected = [&state](const std::size_t i) { return static_cast<int>(i) == state.selection; };

    switch (state.flow) {
    case startup_flow::main: {
        menu.title = lookup_text(text, text_menu_main_title);
        menu.subtitle = !signed_in(online) ? lookup_text(text, text_menu_main_subtitle)
            : online.name.empty()          ? lookup_text(text, text_menu_main_signed_in_unnamed)
                                           : format_text(text, text_menu_main_signed_in, {{"name", online.name}});
        menu.footer = lookup_text(text, text_menu_main_footer);
        menu.version = format_text(text, text_menu_main_version, {{"version", GOLFPP_VERSION}});
        menu.credits = lookup_text(text, text_menu_main_credits);
        const std::vector<main_menu_item> items = main_menu_items(online);
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto [title, hint] = main_menu_text(items[i]);
            add_tile(menu, lookup_text(text, title), lookup_text(text, hint), selected(i));
        }
        break;
    }
    case startup_flow::settings:
        return make_settings_menu_render_data(state.selection, state.settings, catalog.settings, text);
    case startup_flow::help:
        menu.title = lookup_text(text, text_menu_help_title);
        menu.subtitle = lookup_text(text, text_menu_help_subtitle);
        menu.footer = lookup_text(text, text_menu_help_footer);
        break;
    case startup_flow::course_picker:
        menu.title = lookup_text(text, text_menu_course_picker_title);
        menu.subtitle = lookup_text(text, text_menu_course_picker_subtitle);
        menu.footer = lookup_text(text, text_menu_course_picker_footer);
        add_course_tiles(menu, catalog, text, state.selection);
        break;
    default:
        break;
    }
    return menu;
}

render_startup_menu make_confirm_menu_render_data(const startup_flow_state& state,
                                                  const text_assets& text,
                                                  const std::vector<setting_definition>& settings) {
    if (state.confirm_settings) {
        return make_settings_menu_render_data(state.settings_selection, state.settings, settings, text);
    }
    render_startup_menu menu;
    menu.screen = startup_menu_screen::confirm;
    menu.title = lookup_text(text, text_menu_confirm_title);
    add_tile(menu, lookup_text(text, text_menu_confirm_yes), "", state.confirm_selection == 0);
    add_tile(menu, lookup_text(text, text_menu_confirm_no), "", state.confirm_selection == 1);
    add_tile(menu, lookup_text(text, text_menu_confirm_settings), "", state.confirm_selection == confirm_settings_item);
    return menu;
}
