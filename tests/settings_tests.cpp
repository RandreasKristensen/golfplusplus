#include "doctest.h"

#include "audio/audio_manifest.h"
#include "core/settings_menu.h"
#include "core/startup_flow.h"
#include "game/settings.h"
#include "game/text_assets.h"

#include "test_support.h"

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace {
const char* volume_definitions = R"({
  "settings": [
    { "id": "music_volume", "label": "menu.settings.music_volume", "value": "menu.settings.percent",
      "min": 0, "max": 100, "step": 10, "default": 80 }
  ]
})";

std::vector<setting_definition> volume_only() {
    return *parse_setting_definitions_from_text(volume_definitions);
}

input_state pressed_right() {
    input_state input;
    input.right.pressed = true;
    return input;
}

input_state pressed_left() {
    input_state input;
    input.left.pressed = true;
    return input;
}
}

TEST_CASE("setting definitions need a range, a step and a default inside it") {
    const std::vector<setting_definition> definitions = volume_only();
    REQUIRE(definitions.size() == 1U);
    CHECK(definitions[0].step == 10);
    CHECK(definitions[0].default_value == 80);

    CHECK(!parse_setting_definitions_from_text(R"({"settings": [{"id": "a", "label": "l", "value": "v",
        "min": 0, "max": 100, "step": 0, "default": 50}]})"));
    CHECK(!parse_setting_definitions_from_text(R"({"settings": [{"id": "a", "label": "l", "value": "v",
        "min": 0, "max": 100, "step": 10, "default": 120}]})"));
    CHECK(!parse_setting_definitions_from_text(R"({"settings": [
        {"id": "a", "label": "l", "value": "v", "min": 0, "max": 1, "step": 1, "default": 0},
        {"id": "a", "label": "l", "value": "v", "min": 0, "max": 1, "step": 1, "default": 0}]})"));
    CHECK(!parse_setting_definitions_from_text("{}"));
}

TEST_CASE("a setting steps by its step and clamps to its range") {
    const setting_definition volume = volume_only()[0];
    settings_values values;
    CHECK(setting_value(values, volume) == 80);
    values = adjust_setting(values, volume, 1);
    CHECK(setting_value(values, volume) == 90);
    values = adjust_setting(values, volume, 5);
    CHECK(setting_value(values, volume) == 100);
    CHECK(near(setting_fraction(values, volume), 1.0f));
    values = adjust_setting(values, volume, -20);
    CHECK(setting_value(values, volume) == 0);
    CHECK(near(setting_fraction(values, volume), 0.0f));

    values[volume.id] = 250;
    CHECK(setting_value(values, volume) == 100);
}

TEST_CASE("settings survive a write and a read, clamped and without unknown ids") {
    const std::vector<setting_definition> definitions = volume_only();
    settings_values values;
    values["music_volume"] = 30;
    const std::optional<settings_values> parsed = parse_settings_from_text(settings_to_text(values, definitions), definitions);
    REQUIRE(parsed.has_value());
    CHECK(*parsed == values);

    const std::optional<settings_values> odd =
        parse_settings_from_text(R"({"version": 1, "values": {"music_volume": 400, "gone": 3}})", definitions);
    REQUIRE(odd.has_value());
    CHECK(odd->at("music_volume") == 100);
    CHECK(odd->count("gone") == 0U);
    CHECK(!parse_settings_from_text("not json", definitions));

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "golfpp_test_settings";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    const std::filesystem::path path = settings_file_path(root);
    CHECK(load_settings(path, definitions).empty());
    REQUIRE(write_settings(path, values, definitions));
    CHECK(load_settings(path, definitions) == values);
    std::filesystem::remove_all(root, error);
}

TEST_CASE("a golfer setting is listed with the rest but never kept in the settings file") {
    const std::vector<setting_definition> definitions = *parse_setting_definitions_from_text(R"({"settings": [
        {"id": "music_volume", "label": "a", "value": "b", "min": 0, "max": 100, "step": 10, "default": 100},
        {"id": "natural_shape", "label": "c", "values": ["d", "e", "f"], "min": 0, "max": 2, "step": 1, "default": 1,
         "golfer": true}]})");
    REQUIRE(definitions.size() == 2U);
    CHECK(!definitions[0].golfer);
    CHECK(definitions[1].golfer);
    settings_values values;
    values["music_volume"] = 30;
    values["natural_shape"] = 2;
    const std::string text = settings_to_text(values, definitions);
    CHECK(text.find("natural_shape") == std::string::npos);
    const settings_values parsed = *parse_settings_from_text(R"({"version": 1, "values": {"natural_shape": 0}})", definitions);
    CHECK(parsed.count("natural_shape") == 0U);
}

TEST_CASE("the settings list scrolls, and the mouse hovers, clicks the side of each arrow and wheels it") {
    const startup_catalog catalog = load_startup_catalog(shipped_content(), shipped_text_assets());
    const std::vector<setting_definition>& definitions = catalog.settings;
    const auto index_of = [&definitions](const char* id) {
        return static_cast<int>(find_setting(definitions, id) - definitions.data());
    };
    const int volume = index_of(setting_music_volume);
    const int datestamp = index_of(setting_datestamp);
    settings_view view;
    const auto tiles = [&] {
        return static_cast<int>(make_settings_menu_render_data(view, {}, definitions, shipped_text_assets()).tiles.size());
    };
    // More settings than fit: the list shows some, then BACK.
    const int count = tiles();
    REQUIRE(count < static_cast<int>(definitions.size()) + 1);
    REQUIRE(volume < count - 1);
    REQUIRE(datestamp >= count - 1);
    CHECK(make_settings_menu_render_data(view, {}, definitions, shipped_text_assets()).more_below);

    const glm::vec2 half = startup_tile_half_size(startup_menu_screen::main, count);
    const auto tile = [&](const int index) { return startup_tile_center(startup_menu_screen::main, index, count); };
    const glm::vec2 right(half.x * 0.5f, 0.0f);
    settings_values values;
    values[setting_music_volume] = 50;
    std::vector<ui_sound> sounds;
    const auto update = [&](const input_state& input, const menu_pointer& pointer) {
        return update_settings_menu(view, values, definitions, catalog.setting_labels, input, pointer, sounds);
    };

    settings_menu_result result = update(input_state{}, menu_pointer{tile(volume), std::nullopt});
    CHECK(view.selection == volume);
    CHECK(!result.changed);

    // The ">" is right of the value and the "<" left of it.
    result = update(input_state{}, menu_pointer{std::nullopt, tile(volume) + right});
    CHECK(result.changed);
    CHECK(values.at(setting_music_volume) == 60);
    update(input_state{}, menu_pointer{std::nullopt, tile(volume) - right});
    update(input_state{}, menu_pointer{std::nullopt, tile(volume) - right});
    CHECK(values.at(setting_music_volume) == 40);

    // The wheel scrolls the list, not the value.
    input_state wheel;
    wheel.wheel_steps = -(datestamp - (count - 2));
    update(wheel, menu_pointer{});
    CHECK(values.at(setting_music_volume) == 40);
    CHECK(view.scroll == datestamp - (count - 2));
    const render_startup_menu scrolled = make_settings_menu_render_data(view, values, definitions, shipped_text_assets());
    CHECK(scrolled.more_above);
    CHECK(scrolled.tiles[static_cast<std::size_t>(count - 2)].title == "DATESTAMP");

    // On and off switch with a click on either side.
    update(input_state{}, menu_pointer{std::nullopt, tile(count - 2) + right});
    CHECK(view.selection == datestamp);
    CHECK(values.at(setting_datestamp) == 0);
    update(input_state{}, menu_pointer{std::nullopt, tile(count - 2) - right});
    CHECK(values.at(setting_datestamp) == 1);

    // BACK stays under the list.
    result = update(input_state{}, menu_pointer{std::nullopt, tile(count - 1)});
    CHECK(result.back);
}

TEST_CASE("typing on the settings screen searches the settings by name, backspace takes it back, and never leaves") {
    const startup_catalog catalog = load_startup_catalog(shipped_content(), shipped_text_assets());
    settings_view view;
    settings_values values;
    std::vector<ui_sound> sounds;
    input_state typed;
    typed.text_typed = "vol";
    update_settings_menu(view, values, catalog.settings, catalog.setting_labels, typed, menu_pointer{}, sounds);
    CHECK(view.search == "VOL");
    const render_startup_menu menu = make_settings_menu_render_data(view, values, catalog.settings, shipped_text_assets());
    REQUIRE(menu.tiles.size() == 3U);
    CHECK(menu.tiles[0].title == "MASTER VOLUME");
    CHECK(menu.tiles[1].title == "MUSIC VOLUME");
    CHECK(menu.message == "SEARCH: VOL");

    // Right turns the first match up; nothing else moved.
    values[setting_master_volume] = 50;
    update_settings_menu(view, values, catalog.settings, catalog.setting_labels, pressed_right(), menu_pointer{}, sounds);
    CHECK(values.at(setting_master_volume) == 60);

    input_state backspace;
    backspace.backspace.pressed = true;
    for (int i = 0; i < 3; ++i) {
        CHECK(!update_settings_menu(view, values, catalog.settings, catalog.setting_labels, backspace, menu_pointer{}, sounds).back);
    }
    CHECK(view.search.empty());
    CHECK(!update_settings_menu(view, values, catalog.settings, catalog.setting_labels, backspace, menu_pointer{}, sounds).back);
    input_state escape;
    escape.escape.pressed = true;
    CHECK(update_settings_menu(view, values, catalog.settings, catalog.setting_labels, escape, menu_pointer{}, sounds).back);
}

TEST_CASE("the settings screen changes the selected setting with left and right and goes back") {
    const std::vector<setting_definition> definitions = volume_only();
    const std::vector<std::string> labels{"MUSIC VOLUME"};
    settings_values values;
    settings_view view;
    std::vector<ui_sound> sounds;

    settings_menu_result result = update_settings_menu(view, values, definitions, labels, pressed_left(), menu_pointer{}, sounds);
    CHECK(result.changed);
    CHECK(view.selection == 0);
    CHECK(values.at("music_volume") == 70);

    // Already at the top: nothing changes, nothing is saved.
    values["music_volume"] = 100;
    result = update_settings_menu(view, values, definitions, labels, pressed_right(), menu_pointer{}, sounds);
    CHECK(!result.changed);

    input_state down;
    down.down.pressed = true;
    update_settings_menu(view, values, definitions, labels, down, menu_pointer{}, sounds);
    CHECK(view.selection == 1);
    input_state accept;
    accept.enter.pressed = true;
    result = update_settings_menu(view, values, definitions, labels, accept, menu_pointer{}, sounds);
    CHECK(result.back);

    const render_startup_menu menu = make_settings_menu_render_data(settings_view{}, values, definitions, shipped_text_assets());
    REQUIRE(menu.tiles.size() == 2U);
    CHECK(menu.tiles[0].title == "MUSIC VOLUME");
    CHECK(menu.tiles[0].subtitle == "< 100% >");
    CHECK(menu.tiles[1].title == "BACK");
}

TEST_CASE("settings open from the main menu and the in-round menu, and are kept on the way back") {
    startup_catalog catalog;
    catalog.settings = volume_only();
    catalog.setting_labels = {"MUSIC VOLUME"};
    startup_flow_state state;
    state.flow = startup_flow::settings;
    const startup_menu_result changed = update_startup_menu(state, pressed_right(), menu_pointer{}, catalog);
    CHECK(changed.settings_changed);
    input_state escape;
    escape.escape.pressed = true;
    update_startup_menu(state, escape, menu_pointer{}, catalog);
    CHECK(state.flow == startup_flow::main);
    CHECK(state.settings.at("music_volume") == 90);

    enter_playing(state);
    open_confirm_menu(state);
    state.confirm_selection = 2;
    input_state accept;
    accept.enter.pressed = true;
    confirm_menu_result result = update_confirm_menu(state, accept, menu_pointer{}, catalog);
    CHECK(state.confirm_settings);
    CHECK(state.confirm_active);
    CHECK(wants_text_input(state));
    result = update_confirm_menu(state, pressed_left(), menu_pointer{}, catalog);
    CHECK(result.settings_changed);
    CHECK(state.settings.at("music_volume") == 80);
    update_confirm_menu(state, escape, menu_pointer{}, catalog);
    CHECK(!state.confirm_settings);
    CHECK(state.confirm_active);
    CHECK(!result.leave_round);
}

TEST_CASE("the shipped settings have their text and include the ones the game applies") {
    const game_content& content = shipped_content();
    REQUIRE(has_applied_settings(content.settings));
    // The camcorder's datestamp shows unless the player turns it off.
    CHECK(setting_value({}, *find_setting(content.settings, setting_datestamp)) == 1);
    // So does the interact prompt.
    CHECK(setting_value({}, *find_setting(content.settings, setting_interact_prompt)) == 1);
    for (const setting_definition& definition : content.settings) {
        CHECK(shipped_text_assets().strings.entries.count(definition.label_key) == 1U);
        if (definition.value_keys.empty()) {
            CHECK(shipped_text_assets().strings.entries.count(definition.value_key) == 1U);
        }
        for (const std::string& key : definition.value_keys) {
            CHECK(shipped_text_assets().strings.entries.count(key) == 1U);
        }
    }
}

TEST_CASE("the master and music volume settings become the audio levels, and persist") {
    const std::vector<setting_definition> definitions = *parse_setting_definitions_from_text(R"({
  "settings": [
    { "id": "master_volume", "label": "l", "value": "v", "min": 0, "max": 100, "step": 10, "default": 100 },
    { "id": "music_volume", "label": "l", "value": "v", "min": 0, "max": 100, "step": 10, "default": 100 }
  ]
})");
    audio_levels levels = audio_levels_from_settings({}, definitions);
    CHECK(near(levels.master, 1.0f));
    CHECK(near(levels.music, 1.0f));

    settings_values values = adjust_setting({}, definitions[0], -4);
    values = adjust_setting(values, definitions[1], -5);
    levels = audio_levels_from_settings(values, definitions);
    CHECK(near(levels.master, 0.6f));
    CHECK(near(levels.music, 0.5f));
    CHECK(near(played_gain(1.0f, audio_sound_type::ambience, levels), 0.3f));
    CHECK(near(played_gain(1.0f, audio_sound_type::sfx, levels), 0.6f));

    const std::optional<settings_values> parsed = parse_settings_from_text(settings_to_text(values, definitions), definitions);
    REQUIRE(parsed.has_value());
    CHECK(parsed->at("master_volume") == 60);
    CHECK(near(audio_levels_from_settings(*parsed, definitions).master, 0.6f));
}

TEST_CASE("master volume is the first shipped setting") {
    const game_content& content = shipped_content();
    REQUIRE(!content.settings.empty());
    CHECK(content.settings[0].id == setting_master_volume);
    const render_startup_menu menu =
        make_settings_menu_render_data(settings_view{}, {}, content.settings, shipped_text_assets());
    CHECK(menu.tiles[0].title == "MASTER VOLUME");
    CHECK(menu.tiles[0].subtitle == "< 100% >");
}

TEST_CASE("a setting can name each of its values instead of formatting the number") {
    const std::optional<std::vector<setting_definition>> named = parse_setting_definitions_from_text(R"({"settings": [
        {"id": "a", "label": "menu.settings.fullscreen", "values": ["menu.settings.off", "menu.settings.on"],
         "min": 0, "max": 1, "step": 1, "default": 0}]})");
    REQUIRE(named.has_value());
    REQUIRE(named->size() == 1U);
    CHECK((named->at(0).value_keys == std::vector<std::string>{"menu.settings.off", "menu.settings.on"}));

    settings_values values;
    render_startup_menu menu = make_settings_menu_render_data(settings_view{}, values, *named, shipped_text_assets());
    CHECK(menu.tiles[0].subtitle == "< OFF >");
    values = adjust_setting(values, named->at(0), 1);
    CHECK(setting_step_index(values, named->at(0)) == 1);
    menu = make_settings_menu_render_data(settings_view{}, values, *named, shipped_text_assets());
    CHECK(menu.tiles[0].subtitle == "< ON >");

    // One key per step, the range a whole number of steps, and not both kinds of text.
    CHECK(!parse_setting_definitions_from_text(R"({"settings": [{"id": "a", "label": "l", "values": ["off"],
        "min": 0, "max": 1, "step": 1, "default": 0}]})"));
    CHECK(!parse_setting_definitions_from_text(R"({"settings": [{"id": "a", "label": "l", "values": ["a", "b", "c"],
        "min": 0, "max": 1, "step": 1, "default": 0}]})"));
    CHECK(!parse_setting_definitions_from_text(R"({"settings": [{"id": "a", "label": "l", "values": ["a", "b"],
        "min": 0, "max": 5, "step": 3, "default": 0}]})"));
    CHECK(!parse_setting_definitions_from_text(R"({"settings": [{"id": "a", "label": "l", "values": ["a", 3],
        "min": 0, "max": 1, "step": 1, "default": 0}]})"));
    CHECK(!parse_setting_definitions_from_text(R"({"settings": [{"id": "a", "label": "l", "value": "v", "values": ["a", "b"],
        "min": 0, "max": 1, "step": 1, "default": 0}]})"));
    CHECK(!parse_setting_definitions_from_text(R"({"settings": [{"id": "a", "label": "l",
        "min": 0, "max": 1, "step": 1, "default": 0}]})"));
}
