#include "core/online_menus.h"

#include "core/menu_controls.h"
#include "game/text_ids.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace {
enum class login_tile {
    sign_in,
    retry,
    cancel,
    back
};

bool connected(const online_menu_status& online) {
    return online.status == net_status::connected;
}

bool idle(const online_menu_status& online) {
    return online.status == net_status::signed_out || online.status == net_status::failed;
}

// Not signed in, and nothing went wrong: the player just has to sign in.
bool needs_sign_in(const online_menu_status& online) {
    return online.failure_id.empty() || online.failure_id == net_failure_no_stored_sign_in;
}

std::vector<login_tile> login_tiles(const online_menu_status& online) {
    if (!online.available) {
        return {login_tile::back};
    }
    if (idle(online)) {
        return {needs_sign_in(online) ? login_tile::sign_in : login_tile::retry, login_tile::back};
    }
    return {login_tile::cancel};
}

void request(startup_menu_result& result, const online_request_type type, std::string text = {}) {
    result.requests.push_back(online_request{type, std::move(text)});
}

void open_field(startup_flow_state& state, const startup_flow flow, std::string allowed_chars, const std::size_t max_length) {
    open_screen(state, flow);
    state.field.allowed_chars = std::move(allowed_chars);
    state.field.max_length = max_length;
    state.field.active = true;
}

void open_name_entry(startup_flow_state& state, const startup_catalog& catalog) {
    open_field(state, startup_flow::name_entry, catalog.name_chars, catalog.name_max_length);
}

void open_link_code_entry(startup_flow_state& state) {
    open_field(state, startup_flow::link_code_entry, link_code_alphabet, static_cast<std::size_t>(link_code_length));
}

// Signed in: a new account picks a name first.
void open_signed_in(startup_flow_state& state, const startup_catalog& catalog, const online_menu_status& online) {
    if (online.name.empty()) {
        open_name_entry(state, catalog);
    } else {
        open_screen(state, startup_flow::online_course_picker);
    }
}

void request_link_code(startup_flow_state& state, const online_menu_status& online, startup_menu_result& result) {
    if (online.link_code.empty() && !state.waiting) {
        request(result, online_request_type::create_link_code);
        state.waiting = true;
    }
}

// Moves on when the connection or the server says so. True when it changed
// the screen: this frame's input belongs to the screen it left.
bool follow_server(startup_flow_state& state,
                   const startup_catalog& catalog,
                   const online_menu_status& online,
                   startup_menu_result& result) {
    if (state.flow != startup_flow::online_login && !connected(online)) {
        open_online_login(state);
        return true;
    }
    switch (state.flow) {
    case startup_flow::online_login:
        if (connected(online) && online.account_id != 0) {
            open_signed_in(state, catalog, online);
            return true;
        }
        return false;
    case startup_flow::name_entry:
        if (!online.name.empty()) {
            open_screen(state, startup_flow::online_course_picker);
            return true;
        }
        return false;
    case startup_flow::link_code_entry:
        // Linked: this login's account is now the code's.
        if (state.waiting && online.account_id != 0 && online.account_id != state.account_at_submit) {
            open_signed_in(state, catalog, online);
            return true;
        }
        if (state.waiting && online.link_results != state.link_results_at_submit &&
            online.link_result != link_result_linked) {
            state.waiting = false;
            state.message_key = online_link_text_key(online.link_result);
        }
        return false;
    case startup_flow::link_code_show:
        if (!online.link_code.empty()) {
            state.waiting = false;
        } else if (state.message_key.empty()) {
            request_link_code(state, online, result);  // the last one expired
        }
        return false;
    case startup_flow::joining:
        if (online.room_course_id == state.joining_course.id) {
            result.action = startup_action::start_course;
            result.course = state.joining_course;
            result.play = play_mode::online;
        }
        return false;
    default:
        return false;
    }
}

// The server refused what this screen asked for: say why.
void show_refusals(startup_flow_state& state, const online_menu_status& online) {
    for (const reducer_failure& failure : online.reducer_failures) {
        const bool ours = (state.flow == startup_flow::name_entry && failure.reducer == "claim_name") ||
            (state.flow == startup_flow::link_code_entry && failure.reducer == "redeem_link_code") ||
            (state.flow == startup_flow::link_code_show && failure.reducer == "create_link_code") ||
            (state.flow == startup_flow::joining && failure.reducer == "join_course");
        if (!ours) {
            continue;
        }
        if (state.flow == startup_flow::joining) {
            open_screen(state, startup_flow::online_course_picker);
        }
        state.waiting = false;
        state.message_key = online_error_text_key(failure.error);
    }
}

bool accepted(const input_state& input, const int hit) {
    // Not space: names have spaces in them.
    return input.enter.pressed || hit >= 0;
}

void leave_to_main(startup_flow_state& state, const online_menu_status& online, startup_menu_result& result) {
    if (!idle(online) && !connected(online)) {
        request(result, online_request_type::cancel_login);
    }
    return_to_main_menu(state);
}

void update_login(startup_flow_state& state,
                  const input_state& input,
                  const std::optional<glm::vec2> click,
                  const online_menu_status& online,
                  startup_menu_result& result) {
    const std::vector<login_tile> tiles = login_tiles(online);
    const int count = static_cast<int>(tiles.size());
    const int hit = select_with_input(startup_menu_screen::form, 1, state.selection, count, input, click, result.sounds);
    if (input.escape.pressed || input.backspace.pressed) {
        result.sounds.push_back(ui_sound::back);
        leave_to_main(state, online, result);
        return;
    }
    if (!(accepted(input, hit) || input.space.pressed)) {
        return;
    }
    result.sounds.push_back(ui_sound::select);
    switch (tiles[static_cast<std::size_t>(state.selection)]) {
    case login_tile::sign_in:
    case login_tile::retry:
        request(result, online_request_type::begin_login);
        break;
    case login_tile::cancel:
    case login_tile::back:
        leave_to_main(state, online, result);
        break;
    }
}

void update_name_entry(startup_flow_state& state,
                       const input_state& input,
                       const std::optional<glm::vec2> click,
                       startup_menu_result& result) {
    state.field = apply_text_input(state.field, input.text_typed, input.backspace.pressed);
    const int hit = select_with_input(startup_menu_screen::form, 1, state.selection, 2, input, click, result.sounds);
    if (input.escape.pressed) {
        result.sounds.push_back(ui_sound::back);
        return_to_main_menu(state);
        return;
    }
    if (!accepted(input, hit)) {
        return;
    }
    if (state.selection == 1) {
        result.sounds.push_back(ui_sound::select);
        open_link_code_entry(state);
        return;
    }
    if (state.waiting || state.field.value.empty()) {
        return;
    }
    result.sounds.push_back(ui_sound::select);
    request(result, online_request_type::claim_name, state.field.value);
    state.waiting = true;
    state.message_key.clear();
}

void update_link_code_entry(startup_flow_state& state,
                            const input_state& input,
                            const std::optional<glm::vec2> click,
                            const startup_catalog& catalog,
                            const online_menu_status& online,
                            startup_menu_result& result) {
    state.field = apply_text_input(state.field, input.text_typed, input.backspace.pressed);
    const int hit = select_with_input(startup_menu_screen::form, 1, state.selection, 2, input, click, result.sounds);
    const bool back = input.escape.pressed || (accepted(input, hit) && state.selection == 1);
    if (back) {
        result.sounds.push_back(ui_sound::back);
        open_name_entry(state, catalog);
        return;
    }
    if (!accepted(input, hit) || state.waiting || state.field.value.empty()) {
        return;
    }
    result.sounds.push_back(ui_sound::select);
    request(result, online_request_type::redeem_link_code, state.field.value);
    state.waiting = true;
    state.message_key.clear();
    state.account_at_submit = online.account_id;
    state.link_results_at_submit = online.link_results;
}

void update_online_course_picker(startup_flow_state& state,
                                 const input_state& input,
                                 const std::optional<glm::vec2> click,
                                 const startup_catalog& catalog,
                                 startup_menu_result& result) {
    const int count = static_cast<int>(catalog.courses.size());
    const int hit = select_with_input(startup_menu_screen::course_picker, picker_columns, state.selection, count, input, click,
                                      result.sounds);
    if (input.escape.pressed || input.backspace.pressed) {
        result.sounds.push_back(ui_sound::back);
        return_to_main_menu(state);
        return;
    }
    if (!(input.enter.pressed || input.space.pressed || hit >= 0) || count <= 0) {
        return;
    }
    result.sounds.push_back(ui_sound::select);
    const course_definition course = catalog.courses[static_cast<std::size_t>(state.selection)].course;
    request(result, online_request_type::join_course, course.id);
    open_screen(state, startup_flow::joining);
    state.joining_course = course;
}

void update_waiting_screen(startup_flow_state& state,
                           const input_state& input,
                           const std::optional<glm::vec2> click,
                           startup_menu_result& result) {
    const int hit = select_with_input(startup_menu_screen::form, 1, state.selection, 1, input, click, result.sounds);
    if (!(accepted(input, hit) || input.space.pressed || input.escape.pressed || input.backspace.pressed)) {
        return;
    }
    result.sounds.push_back(input.escape.pressed || input.backspace.pressed ? ui_sound::back : ui_sound::select);
    if (state.flow == startup_flow::joining) {
        request(result, online_request_type::leave_room);
        open_screen(state, startup_flow::online_course_picker);
    } else {
        return_to_main_menu(state);
    }
}

std::string message_text(const text_assets& text, const std::string& key) {
    return lookup_text(text, key.c_str());
}

void login_render_data(const text_assets& text, const online_menu_status& online, render_startup_menu& menu) {
    if (!online.available) {
        menu.message = lookup_text(text, text_menu_login_unavailable);
        menu.message_is_error = true;
    } else if (idle(online) && needs_sign_in(online)) {
        menu.message = lookup_text(text, text_menu_login_needed);
    } else if (idle(online)) {
        menu.message = message_text(text, online_error_text_key(online.failure_id));
        menu.message_is_error = true;
    } else if (online.status == net_status::waiting_for_browser) {
        menu.message = lookup_text(text, text_menu_login_browser);
    } else {
        menu.message = lookup_text(text, text_menu_login_signing_in);
    }
}

const char* login_tile_key(const login_tile tile) {
    switch (tile) {
    case login_tile::sign_in:
        return text_menu_login_sign_in;
    case login_tile::retry:
        return text_menu_login_retry;
    case login_tile::cancel:
        return text_menu_login_cancel;
    case login_tile::back:
        return text_menu_login_back;
    }
    return text_menu_login_back;
}

const char* login_tile_hint_key(const login_tile tile) {
    switch (tile) {
    case login_tile::sign_in:
        return text_menu_login_sign_in_hint;
    case login_tile::retry:
        return text_menu_login_retry_hint;
    case login_tile::cancel:
        return text_menu_login_cancel_hint;
    case login_tile::back:
        return text_menu_login_back_hint;
    }
    return text_menu_login_back_hint;
}
}

bool is_online_flow(const startup_flow flow) {
    switch (flow) {
    case startup_flow::online_login:
    case startup_flow::name_entry:
    case startup_flow::link_code_entry:
    case startup_flow::link_code_show:
    case startup_flow::online_course_picker:
    case startup_flow::joining:
        return true;
    case startup_flow::main:
    case startup_flow::help:
    case startup_flow::offline:
    case startup_flow::hole_picker:
    case startup_flow::course_picker:
    case startup_flow::playing:
        return false;
    }
    return false;
}

void open_play_online(startup_flow_state& state, const online_menu_status& online, startup_menu_result& result) {
    if (connected(online) && online.account_id != 0 && !online.name.empty()) {
        open_screen(state, startup_flow::online_course_picker);
        return;
    }
    open_online_login(state);
    if (online.available && idle(online)) {
        request(result, online_request_type::begin_silent_login);
    }
}

void open_link_code_show(startup_flow_state& state, const online_menu_status& online, startup_menu_result& result) {
    open_screen(state, startup_flow::link_code_show);
    request_link_code(state, online, result);
}

void update_online_menu(startup_flow_state& state,
                        const input_state& input,
                        const std::optional<glm::vec2> click,
                        const startup_catalog& catalog,
                        const online_menu_status& online,
                        startup_menu_result& result) {
    if (follow_server(state, catalog, online, result)) {
        return;
    }
    show_refusals(state, online);

    switch (state.flow) {
    case startup_flow::online_login:
        update_login(state, input, click, online, result);
        break;
    case startup_flow::name_entry:
        update_name_entry(state, input, click, result);
        break;
    case startup_flow::link_code_entry:
        update_link_code_entry(state, input, click, catalog, online, result);
        break;
    case startup_flow::online_course_picker:
        update_online_course_picker(state, input, click, catalog, result);
        break;
    case startup_flow::link_code_show:
    case startup_flow::joining:
        update_waiting_screen(state, input, click, result);
        break;
    default:
        break;
    }
}

void make_online_menu_render_data(const startup_flow_state& state,
                                  const startup_catalog& catalog,
                                  const text_assets& text,
                                  const online_menu_status& online,
                                  render_startup_menu& menu) {
    menu.screen = startup_menu_screen::form;
    menu.title = lookup_text(text, text_menu_online_title);
    menu.footer = lookup_text(text, text_menu_online_footer);
    menu.cursor_time = static_cast<float>(std::fmod(state.clock_seconds, 60.0));
    if (!state.message_key.empty()) {
        menu.message = message_text(text, state.message_key);
        menu.message_is_error = true;
    }
    const auto selected = [&state](const std::size_t i) { return static_cast<int>(i) == state.selection; };

    switch (state.flow) {
    case startup_flow::online_login: {
        menu.subtitle = lookup_text(text, text_menu_login_subtitle);
        login_render_data(text, online, menu);
        const std::vector<login_tile> tiles = login_tiles(online);
        for (std::size_t i = 0; i < tiles.size(); ++i) {
            add_tile(menu, lookup_text(text, login_tile_key(tiles[i])), lookup_text(text, login_tile_hint_key(tiles[i])), selected(i));
        }
        break;
    }
    case startup_flow::name_entry:
        menu.title = lookup_text(text, text_menu_name_title);
        menu.subtitle = lookup_text(text, text_menu_name_subtitle);
        menu.footer = lookup_text(text, text_menu_name_footer);
        menu.field = state.field;
        if (state.waiting) {
            menu.message = lookup_text(text, text_menu_name_waiting);
        }
        add_tile(menu, lookup_text(text, text_menu_name_ok), lookup_text(text, text_menu_name_ok_hint), selected(0));
        add_tile(menu, lookup_text(text, text_menu_name_have_account), lookup_text(text, text_menu_name_have_account_hint), selected(1));
        break;
    case startup_flow::link_code_entry:
        menu.title = lookup_text(text, text_menu_link_entry_title);
        menu.subtitle = lookup_text(text, text_menu_link_entry_subtitle);
        menu.footer = lookup_text(text, text_menu_link_entry_footer);
        menu.field = state.field;
        if (state.waiting) {
            menu.message = lookup_text(text, text_menu_link_entry_waiting);
        }
        add_tile(menu, lookup_text(text, text_menu_link_entry_ok), lookup_text(text, text_menu_link_entry_ok_hint), selected(0));
        add_tile(menu, lookup_text(text, text_menu_login_back), lookup_text(text, text_menu_link_entry_back_hint), selected(1));
        break;
    case startup_flow::link_code_show:
        menu.title = lookup_text(text, text_menu_link_code_title);
        menu.subtitle = lookup_text(text, text_menu_link_code_subtitle);
        menu.footer = lookup_text(text, text_menu_link_code_footer);
        if (!online.link_code.empty()) {
            menu.code = online.link_code;
            menu.message = format_text(text, text_menu_link_code_expires, {{"minutes", std::to_string(online.link_code_minutes_left)}});
            menu.message_is_error = false;
        } else if (state.message_key.empty()) {
            menu.message = lookup_text(text, text_menu_link_code_waiting);
        }
        add_tile(menu, lookup_text(text, text_menu_link_code_done), lookup_text(text, text_menu_link_code_done_hint), selected(0));
        break;
    case startup_flow::online_course_picker:
        menu.screen = startup_menu_screen::course_picker;
        menu.subtitle = lookup_text(text, text_menu_online_course_picker_subtitle);
        menu.footer = lookup_text(text, text_menu_course_picker_footer);
        add_course_tiles(menu, catalog, text, state.selection);
        break;
    case startup_flow::joining:
        menu.subtitle = lookup_text(text, text_menu_joining_subtitle);
        menu.message = format_text(text, text_menu_joining_message, {{"course", state.joining_course.name}});
        add_tile(menu, lookup_text(text, text_menu_login_cancel), lookup_text(text, text_menu_joining_cancel_hint), selected(0));
        break;
    default:
        break;
    }
}
