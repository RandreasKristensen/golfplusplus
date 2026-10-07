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
    const std::optional<settings_values> parsed = parse_settings_from_text(settings_to_text(values), definitions);
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
    REQUIRE(write_settings(path, values));
    CHECK(load_settings(path, definitions) == values);
    std::filesystem::remove_all(root, error);
}

TEST_CASE("the settings screen changes the selected setting with left and right and goes back") {
    const std::vector<setting_definition> definitions = volume_only();
    settings_values values;
    int selection = 0;
    std::vector<ui_sound> sounds;

    settings_menu_result result = update_settings_menu(selection, values, definitions, pressed_left(), std::nullopt, sounds);
    CHECK(result.changed);
    CHECK(selection == 0);
    CHECK(values.at("music_volume") == 70);

    // Already at the top: nothing changes, nothing is saved.
    values["music_volume"] = 100;
    result = update_settings_menu(selection, values, definitions, pressed_right(), std::nullopt, sounds);
    CHECK(!result.changed);

    input_state down;
    down.down.pressed = true;
    update_settings_menu(selection, values, definitions, down, std::nullopt, sounds);
    CHECK(selection == 1);
    input_state accept;
    accept.enter.pressed = true;
    result = update_settings_menu(selection, values, definitions, accept, std::nullopt, sounds);
    CHECK(result.back);

    const render_startup_menu menu = make_settings_menu_render_data(0, values, definitions, shipped_text_assets());
    REQUIRE(menu.tiles.size() == 2U);
    CHECK(menu.tiles[0].title == "MUSIC VOLUME");
    CHECK(menu.tiles[0].subtitle == "< 100% >");
    CHECK(menu.tiles[1].title == "BACK");
}

TEST_CASE("settings open from the main menu and the in-round menu, and are kept on the way back") {
    startup_catalog catalog;
    catalog.settings = volume_only();
    startup_flow_state state;
    state.flow = startup_flow::settings;
    const startup_menu_result changed = update_startup_menu(state, pressed_right(), std::nullopt, catalog);
    CHECK(changed.settings_changed);
    input_state escape;
    escape.escape.pressed = true;
    update_startup_menu(state, escape, std::nullopt, catalog);
    CHECK(state.flow == startup_flow::main);
    CHECK(state.settings.at("music_volume") == 90);

    enter_playing(state);
    open_confirm_menu(state);
    state.confirm_selection = 2;
    input_state accept;
    accept.enter.pressed = true;
    confirm_menu_result result = update_confirm_menu(state, accept, std::nullopt, catalog.settings);
    CHECK(state.confirm_settings);
    CHECK(state.confirm_active);
    result = update_confirm_menu(state, pressed_left(), std::nullopt, catalog.settings);
    CHECK(result.settings_changed);
    CHECK(state.settings.at("music_volume") == 80);
    update_confirm_menu(state, escape, std::nullopt, catalog.settings);
    CHECK(!state.confirm_settings);
    CHECK(state.confirm_active);
    CHECK(!result.leave_round);
}

TEST_CASE("the shipped settings have their text and include the ones the game applies") {
    const game_content& content = shipped_content();
    REQUIRE(has_applied_settings(content.settings));
    for (const setting_definition& definition : content.settings) {
        CHECK(shipped_text_assets().strings.entries.count(definition.label_key) == 1U);
        CHECK(shipped_text_assets().strings.entries.count(definition.value_key) == 1U);
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

    const std::optional<settings_values> parsed = parse_settings_from_text(settings_to_text(values), definitions);
    REQUIRE(parsed.has_value());
    CHECK(parsed->at("master_volume") == 60);
    CHECK(near(audio_levels_from_settings(*parsed, definitions).master, 0.6f));
}

TEST_CASE("master volume is the first shipped setting") {
    const game_content& content = shipped_content();
    REQUIRE(!content.settings.empty());
    CHECK(content.settings[0].id == setting_master_volume);
    const render_startup_menu menu =
        make_settings_menu_render_data(0, {}, content.settings, shipped_text_assets());
    CHECK(menu.tiles[0].title == "MASTER VOLUME");
    CHECK(menu.tiles[0].subtitle == "< 100% >");
}
