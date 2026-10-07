#include "doctest.h"

#include "core/startup_flow.h"
#include "game/text_assets.h"
#include "renderer/menu_overlay.h"

#include "test_support.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace {
bool has_sound(const std::vector<ui_sound>& sounds, const ui_sound sound) {
    return std::find(sounds.begin(), sounds.end(), sound) != sounds.end();
}

startup_catalog two_courses() {
    course_definition first;
    first.id = "first";
    first.name = "First";
    course_definition second;
    second.id = "second";
    second.name = "Second";
    startup_catalog catalog;
    catalog.courses = {startup_course_option{first, 0, std::nullopt}, startup_course_option{second, 0, std::nullopt}};
    return catalog;
}
}

TEST_CASE("main menu opens online, offline, help and settings, and quits from the last item") {
    const startup_catalog catalog = two_courses();

    startup_flow_state state;
    input_state input;
    input.enter.pressed = true;
    update_startup_menu(state, input, std::nullopt, catalog);
    CHECK(state.flow == startup_flow::online_login);

    return_to_main_menu(state);
    state.selection = 1;
    update_startup_menu(state, input, std::nullopt, catalog);
    CHECK(state.flow == startup_flow::course_picker);

    return_to_main_menu(state);
    state.selection = 2;
    update_startup_menu(state, input, std::nullopt, catalog);
    CHECK(state.flow == startup_flow::help);

    return_to_main_menu(state);
    state.selection = 3;
    update_startup_menu(state, input, std::nullopt, catalog);
    CHECK(state.flow == startup_flow::settings);

    return_to_main_menu(state);
    state.selection = 4;
    const startup_menu_result result = update_startup_menu(state, input, std::nullopt, catalog);
    CHECK(result.action == startup_action::quit);
    CHECK(has_sound(result.sounds, ui_sound::select));
}

TEST_CASE("signed in, the main menu adds linking and signing out") {
    online_menu_status online;
    online.available = true;
    online.status = net_status::connected;
    online.account_id = 7;
    online.name = "ANNA";

    startup_flow_state state;
    state.selection = 3;
    input_state input;
    input.enter.pressed = true;
    startup_menu_result result = update_startup_menu(state, input, std::nullopt, two_courses(), online);
    REQUIRE(result.requests.size() == 1U);
    CHECK(result.requests[0].type == online_request_type::sign_out);

    // Signed out, the same place is SETTINGS; a selection past the end moves back.
    online = online_menu_status{};
    state.selection = 6;
    result = update_startup_menu(state, input_state{}, std::nullopt, two_courses(), online);
    CHECK(state.selection == 4);
    CHECK(result.requests.empty());
}

TEST_CASE("a guest's main menu has signing out but no linking") {
    online_menu_status online;
    online.available = true;
    online.status = net_status::connected;
    online.account_id = 7;
    online.name = "ANNA";
    online.guest = true;

    // PLAY ONLINE, PLAY OFFLINE, SIGN OUT: the third item signs out.
    startup_flow_state state;
    state.selection = 2;
    input_state input;
    input.enter.pressed = true;
    const startup_menu_result result = update_startup_menu(state, input, std::nullopt, two_courses(), online);
    REQUIRE(result.requests.size() == 1U);
    CHECK(result.requests[0].type == online_request_type::sign_out);
}

TEST_CASE("back leaves a sub-menu for the main menu and quits from there") {
    const startup_catalog catalog = two_courses();
    startup_flow_state state;
    state.flow = startup_flow::course_picker;
    state.selection = 1;

    input_state input;
    input.escape.pressed = true;
    startup_menu_result result = update_startup_menu(state, input, std::nullopt, catalog);
    CHECK(state.flow == startup_flow::main);
    CHECK(state.selection == 0);
    CHECK(result.action == startup_action::none);
    CHECK(has_sound(result.sounds, ui_sound::back));

    result = update_startup_menu(state, input, std::nullopt, catalog);
    CHECK(result.action == startup_action::quit);
}

TEST_CASE("course picker asks app to start the selected course without leaving the menu") {
    const startup_catalog catalog = two_courses();
    startup_flow_state state;
    state.flow = startup_flow::course_picker;

    input_state input;
    input.right.pressed = true;
    startup_menu_result result = update_startup_menu(state, input, std::nullopt, catalog);
    CHECK(state.selection == 1);
    CHECK(has_sound(result.sounds, ui_sound::move));
    CHECK(result.action == startup_action::none);

    input = input_state{};
    input.enter.pressed = true;
    result = update_startup_menu(state, input, std::nullopt, catalog);
    CHECK(result.action == startup_action::start_course);
    CHECK(result.course.id == "second");
    CHECK(result.play == play_mode::offline);
    CHECK(state.flow == startup_flow::course_picker);

    enter_playing(state);
    CHECK(state.flow == startup_flow::playing);
}

TEST_CASE("clicking a tile selects and accepts it") {
    const startup_catalog catalog = two_courses();
    startup_flow_state state;
    const glm::vec2 help_tile = startup_tile_center(startup_menu_screen::main, 2, 5);
    const startup_menu_result result = update_startup_menu(state, input_state{}, help_tile, catalog);
    CHECK(state.flow == startup_flow::help);
    CHECK(has_sound(result.sounds, ui_sound::move));

    CHECK(startup_tile_at(startup_menu_screen::main, 4, glm::vec2(0.99f, -0.99f)) == -1);
}

TEST_CASE("a long single column closes up so every tile stays above the footer") {
    for (const int count : {2, 4, 6, 8}) {
        const glm::vec2 half = startup_tile_half_size(startup_menu_screen::main, count);
        const glm::vec2 last = startup_tile_center(startup_menu_screen::main, count - 1, count);
        CHECK(last.y - half.y >= -0.78f);
        CHECK(startup_tile_center(startup_menu_screen::main, 0, count).y + half.y <= 0.36f);
        for (int i = 1; i < count; ++i) {
            const float gap = startup_tile_center(startup_menu_screen::main, i - 1, count).y -
                startup_tile_center(startup_menu_screen::main, i, count).y;
            CHECK(gap >= 2.0f * half.y);
        }
    }
    // Up to four tiles keep the usual spacing.
    CHECK(near(startup_tile_center(startup_menu_screen::main, 1, 4).y, startup_tile_center(startup_menu_screen::main, 1, 2).y));
}

TEST_CASE("confirm menu opens on NO and only YES leaves the round") {
    const std::vector<setting_definition> settings;
    startup_flow_state state;
    enter_playing(state);
    open_confirm_menu(state);
    CHECK(state.confirm_active);
    CHECK(state.confirm_selection == 1);

    input_state accept;
    accept.enter.pressed = true;
    confirm_menu_result result = update_confirm_menu(state, accept, std::nullopt, settings);
    CHECK(!result.leave_round);
    CHECK(!state.confirm_active);

    open_confirm_menu(state);
    input_state up;
    up.up.pressed = true;
    update_confirm_menu(state, up, std::nullopt, settings);
    CHECK(state.confirm_selection == 0);
    result = update_confirm_menu(state, accept, std::nullopt, settings);
    CHECK(result.leave_round);

    open_confirm_menu(state);
    input_state cancel;
    cancel.backspace.pressed = true;
    result = update_confirm_menu(state, cancel, std::nullopt, settings);
    CHECK(!result.leave_round);
    CHECK(!state.confirm_active);
    CHECK(has_sound(result.sounds, ui_sound::back));
}

TEST_CASE("menu render data takes its text from the string table") {
    const std::optional<text_assets> text = load_text_assets(GOLFPP_ASSETS_DIR);
    REQUIRE(text.has_value());

    startup_flow_state state;
    const render_startup_menu main = make_startup_menu_render_data(state, two_courses(), *text);
    CHECK(main.screen == startup_menu_screen::main);
    CHECK(main.title == "GOLF++");
    CHECK(main.tiles.size() == 5U);
    CHECK(main.tiles[0].title == "PLAY ONLINE");
    CHECK(main.tiles[0].selected);
    CHECK(main.tiles[1].title == "PLAY OFFLINE");
    CHECK(main.tiles[3].title == "SETTINGS");
    CHECK(main.tiles[4].subtitle == "RETURN TO DESKTOP");

    online_menu_status online;
    online.status = net_status::connected;
    online.account_id = 7;
    online.name = "ANNA";
    const render_startup_menu signed_in = make_startup_menu_render_data(state, two_courses(), *text, online);
    CHECK(signed_in.subtitle == "SIGNED IN AS ANNA");
    CHECK(signed_in.tiles.size() == 7U);
    CHECK(signed_in.tiles[2].title == "LINK ANOTHER LOGIN");
    CHECK(signed_in.tiles[3].title == "SIGN OUT");

    state.flow = startup_flow::course_picker;
    const render_startup_menu courses = make_startup_menu_render_data(state, two_courses(), *text);
    CHECK(courses.tiles.size() == 2U);
    CHECK(courses.tiles[1].title == "Second");
    CHECK(courses.tiles[1].subtitle == "0 HOLES  PAR 0");

    open_confirm_menu(state);
    const render_startup_menu confirm = make_confirm_menu_render_data(state, *text, {});
    CHECK(confirm.screen == startup_menu_screen::confirm);
    CHECK(confirm.title == "LEAVE THE ROUND?");
    CHECK(confirm.tiles.size() == 3U);
    CHECK(confirm.tiles[2].title == "SETTINGS");
    CHECK(confirm.tiles[1].title == "NO");
    CHECK(confirm.tiles[1].selected);
}

TEST_CASE("the catalog totals each course's par and previews hole 1") {
    const startup_catalog catalog = load_startup_catalog(*load_game_content(GOLFPP_ASSETS_DIR).content, shipped_text_assets());
    REQUIRE(!catalog.courses.empty());
    CHECK(catalog.name_max_length > 0U);
    CHECK(catalog.name_chars.find('A') != std::string::npos);
    for (const startup_course_option& option : catalog.courses) {
        CHECK(option.total_par >= static_cast<int>(option.course.holes.size()) * 3);
        CHECK(option.preview.has_value());
    }
}
