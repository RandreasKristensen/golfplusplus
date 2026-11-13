#include "doctest.h"

#include "core/look_tuning.h"
#include "game/content_files.h"
#include "game/json_edit.h"
#include "game/lighting_numbers.h"
#include "game/look_reload.h"
#include "game/tuning_panel.h"
#include "test_support.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

namespace {
const std::string& fixture_lighting() {
    static const std::string text = *read_text_file(std::filesystem::path(fixture_root()) / lighting_path);
    return text;
}

const std::string& shipped_picture() {
    static const std::string text = *read_text_file(std::filesystem::path(asset_root()) / picture_style_path);
    return text;
}

const lighting_palette& fixture_palette() {
    static const lighting_palette palette = *parse_lighting_from_text(fixture_lighting());
    return palette;
}

tuning_entry entry(const std::string& path, const double step) {
    return tuning_entry{"look.sun_glow_at_sun", *parse_json_path(path), step};
}

// `text` with `from` (which it holds once) written as `to`; empty when it
// does not hold it once.
std::string with(std::string text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    if (at == std::string::npos || text.find(from, at + 1) != std::string::npos) {
        return {};
    }
    return text.replace(at, from.size(), to);
}

// The value at `path` in `text`, as written; empty when none.
std::string written_at(const std::string& text, const std::string& path) {
    const std::optional<text_span> span = json_value_span(text, *parse_json_path(path));
    return span ? text.substr(span->begin, span->end - span->begin) : std::string();
}

// A folder with lighting.json and picture.json, the look's files.
struct look_folder {
    std::filesystem::path root;
    std::filesystem::path lighting;
    std::filesystem::path picture;

    explicit look_folder(const char* name)
        : root(std::filesystem::temp_directory_path() / name), lighting(root / lighting_path), picture(root / picture_style_path) {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        REQUIRE(replace_text_file(lighting, fixture_lighting()));
        REQUIRE(replace_text_file(picture, shipped_picture()));
    }
    ~look_folder() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    // Writes `text` to `file` and moves its write time on, so a poll sees it.
    void save(const std::filesystem::path& file, const std::string& text) const {
        const std::filesystem::file_time_type before = *file_write_time(file);
        REQUIRE(replace_text_file(file, text));
        std::error_code error;
        std::filesystem::last_write_time(file, before + std::chrono::seconds(2), error);
        REQUIRE(!error);
    }
};
}

TEST_CASE("a JSON value is replaced at its path and every other byte stays as it was") {
    const std::string text = "{\r\n  \"name\": \"a } \\\" [ b\",\r\n  \"weather\": {\"mist\": 0.5,  \"sun_glow\":0.03 },\r\n"
                             "  \"keys\": [ {\"mist\": 1}, {\"mist\": 2.50} ],\r\n  \"weather\": {\"sun_glow\": 9}\r\n}\r\n";
    CHECK(replace_json_value(text, *parse_json_path("weather/sun_glow"), "0.035") ==
          with(text, "\"sun_glow\":0.03 ", "\"sun_glow\":0.035 "));
    CHECK(replace_json_value(text, *parse_json_path("keys/1/mist"), "2.6") == with(text, "2.50", "2.6"));
    CHECK(written_at(text, "keys/0/mist") == "1");
    CHECK(written_at(text, "name") == "\"a } \\\" [ b\"");
    CHECK(!replace_json_value(text, *parse_json_path("weather/fog"), "1"));
    CHECK(!replace_json_value(text, *parse_json_path("keys/2/mist"), "1"));
    CHECK(!replace_json_value(text, *parse_json_path("keys/x/mist"), "1"));
    CHECK(!replace_json_value("{\"a\": 1", *parse_json_path("a"), "2"));
    CHECK(!parse_json_path("weather//mist"));
    CHECK(!parse_json_path(""));

    CHECK(json_number_text(0.035000001, 3, true) == "0.035");
    CHECK(json_number_text(0.5, 3, true) == "0.5");
    CHECK(json_number_text(2.0, 2, true) == "2.0");
    CHECK(json_number_text(2.0, 2, false) == "2");
    CHECK(json_number_text(-0.0001, 2, false) == "0");
    CHECK(json_number_decimals("0.035") == 3);
    CHECK(json_number_decimals("16") == 0);
    CHECK(json_number_decimals("1.25e-3") == 2);
}

TEST_CASE("a lighting number is found by its path with the range its loader allows") {
    const lighting_palette& palette = fixture_palette();
    const std::optional<lighting_number> glow = lighting_number_at(palette, *parse_json_path("weather/sun_glow_at_sun"));
    REQUIRE(glow.has_value());
    CHECK(near(glow->value, 0.6f));
    CHECK(glow->low == 0.0f);
    CHECK(glow->high == 20.0f);
    CHECK(!glow->whole);

    const std::optional<lighting_number> mist = lighting_number_at(palette, *parse_json_path("keys/2/mist_density"));
    REQUIRE(mist.has_value());
    CHECK(near(mist->value, palette.keys[2].mist_density));

    const std::optional<lighting_number> steps = lighting_number_at(palette, *parse_json_path("shafts/steps"));
    REQUIRE(steps.has_value());
    CHECK(steps->whole);
    CHECK(steps->value == 16.0f);

    CHECK(!lighting_number_at(palette, *parse_json_path("weather/overcast_sky")));
    CHECK(!lighting_number_at(palette, *parse_json_path("shafts/lamp_map_resolution")));
    CHECK(!lighting_number_at(palette, *parse_json_path("keys/99/mist_density")));
    CHECK(!lighting_number_at(palette, *parse_json_path("weather")));

    // -7 degrees is nearest the -6 key.
    CHECK(nearest_lighting_key(palette.keys, -7.0f) == 2U);
    CHECK((tuning_entry_path(entry("keys/@sun/mist_density", 0.001), 2) == json_path{"keys", "2", "mist_density"}));
}

TEST_CASE("a tuning step writes only that number, within the loader's range") {
    const std::string& text = fixture_lighting();
    const tuning_entry glow = entry("weather/sun_glow_at_sun", 0.05);
    const std::optional<std::string> up = step_lighting_text(text, glow, glow.path, 1, false);
    REQUIRE(up.has_value());
    CHECK(*up == with(text, "\"sun_glow_at_sun\": 0.6,", "\"sun_glow_at_sun\": 0.65,"));
    CHECK(near(parse_lighting_from_text(*up)->weather.sun_glow_at_sun, 0.65f));
    // Shift: a tenth of a step.
    CHECK(written_at(*step_lighting_text(text, glow, glow.path, -1, true), "weather/sun_glow_at_sun") == "0.595");

    // Clamped at the top of the range, then no further.
    const tuning_entry forward = entry("weather/clear_forwardness", 0.2);
    std::string stepped = text;
    for (int i = 0; i < 2; ++i) {
        stepped = *step_lighting_text(stepped, forward, forward.path, 1, false);
    }
    CHECK(written_at(stepped, "weather/clear_forwardness") == "0.95");
    CHECK(!step_lighting_text(stepped, forward, forward.path, 1, false));

    // Counts step by whole numbers, from 0 up to their most.
    const tuning_entry lamps = entry("shafts/lamps", 1);
    CHECK(written_at(*step_lighting_text(text, lamps, lamps.path, 1, true), "shafts/lamps") == "5");
    stepped = text;
    for (int i = 0; i < 4; ++i) {
        stepped = *step_lighting_text(stepped, lamps, lamps.path, -1, false);
    }
    CHECK(written_at(stepped, "shafts/lamps") == "0");
    CHECK(!step_lighting_text(stepped, lamps, lamps.path, -1, false));

    // The sun's key: only that key's mist changes.
    const tuning_entry mist = entry("keys/@sun/mist_density", 0.001);
    const json_path at_dusk = tuning_entry_path(mist, nearest_lighting_key(fixture_palette().keys, -5.0f));
    const lighting_palette after = *parse_lighting_from_text(*step_lighting_text(text, mist, at_dusk, 1, false));
    CHECK(near(after.keys[2].mist_density, 0.009f));
    CHECK(near(after.keys[3].mist_density, fixture_palette().keys[3].mist_density));

    // Within its own range, but the loader wants full_deg under on_deg.
    const tuning_entry full = entry("night_lights/full_deg", 100);
    CHECK(!step_lighting_text(text, full, full.path, 1, false));
    CHECK(!step_lighting_text("not json", glow, glow.path, 1, false));
}

TEST_CASE("the tuning panel's list names only ranged numbers of lighting.json") {
    const lighting_palette& palette = fixture_palette();
    const std::optional<std::vector<tuning_entry>> list = parse_tuning_panel_from_text(
        R"({"entries": [{"label": "a", "path": "weather/mist", "step": 0.05},
                        {"label": "b", "path": "keys/@sun/mist_height_meters", "step": 1}]})",
        palette);
    REQUIRE(list.has_value());
    CHECK(list->size() == 2U);
    CHECK(((*list)[1].path == json_path{"keys", sun_key_segment, "mist_height_meters"}));
    CHECK(!parse_tuning_panel_from_text(R"({"entries": [{"label": "a", "path": "weather/overcast_sky", "step": 0.1}]})", palette));
    CHECK(!parse_tuning_panel_from_text(R"({"entries": [{"label": "a", "path": "weather/mist", "step": 0}]})", palette));
    CHECK(!parse_tuning_panel_from_text(R"({"entries": [{"label": "", "path": "weather/mist", "step": 1}]})", palette));
    CHECK(!parse_tuning_panel_from_text(R"({"entries": []})", palette));
}

TEST_CASE("the tuning panel opens only where allowed, its rows wrap, and arrows step") {
    look_tuning_state state;
    state.entries = {entry("weather/mist", 0.05), entry("weather/sun_glow_at_sun", 0.05), entry("shafts/steps", 1)};
    look_tuning_keys toggle;
    toggle.toggle = true;
    CHECK(update_look_panel(state, toggle, false) == 0);
    CHECK(!state.open);
    CHECK(update_look_panel(state, toggle, true) == 0);
    REQUIRE(state.open);

    look_tuning_keys up;
    up.up = true;
    update_look_panel(state, up, true);
    CHECK(state.selected == 2U);
    look_tuning_keys down;
    down.down = true;
    update_look_panel(state, down, true);
    CHECK(state.selected == 0U);

    look_tuning_keys right;
    right.right = true;
    CHECK(update_look_panel(state, right, true) == 1);
    look_tuning_keys left;
    left.left = true;
    CHECK(update_look_panel(state, left, true) == -1);

    // Leaving where it is allowed closes it.
    CHECK(update_look_panel(state, right, false) == 0);
    CHECK(!state.open);
}

TEST_CASE("a reloaded look keeps the last good one through a bad file and says why") {
    look_watch watch;
    watch.shown = look_texts{fixture_lighting(), shipped_picture()};
    CHECK(!take_look_texts(watch, watch.shown));

    const std::string longer = with(fixture_lighting(), "\"steps\": 16,", "\"steps\": 24,");
    const std::optional<look_reload> reload = take_look_texts(watch, look_texts{longer, shipped_picture()});
    REQUIRE(reload.has_value());
    CHECK(reload->next.lighting.shafts.steps == 24);
    CHECK(reload->changes.shafts);
    CHECK(!reload->changes.sky);
    CHECK(!reload->changes.shadows);
    CHECK(!reload->changes.ground);
    CHECK(!reload->changes.picture);
    CHECK(watch.shown.lighting == longer);

    CHECK(!take_look_texts(watch, look_texts{"{ \"keys\": [", shipped_picture()}));
    REQUIRE(watch.error.has_value());
    CHECK(watch.error->problem == look_problem::not_json);
    CHECK(std::string(watch.error->file) == lighting_path);
    CHECK(watch.shown.lighting == longer);

    CHECK(!take_look_texts(watch, look_texts{longer, with(shipped_picture(), "\"target_lines\": 360", "\"target_lines\": -1")}));
    REQUIRE(watch.error.has_value());
    CHECK(watch.error->problem == look_problem::not_valid);
    CHECK(std::string(watch.error->file) == picture_style_path);

    // Saved back as it was shown: nothing to reload, and no error.
    CHECK(!take_look_texts(watch, look_texts{longer, shipped_picture()}));
    CHECK(!watch.error.has_value());
}

TEST_CASE("the look's files are polled, and a good save after a bad one recovers") {
    const look_folder folder("golfpp_test_look_reload");
    look_start start = start_look(folder.root.string());
    REQUIRE(start.loaded.has_value());
    look_watch& watch = start.watch;
    CHECK(!poll_look(watch, look_poll_seconds));

    const std::string misty = with(fixture_lighting(), "\"mist\": 0.5,", "\"mist\": 0.6,");
    folder.save(folder.lighting, misty);
    CHECK(!poll_look(watch, look_poll_seconds * 0.5f));
    std::optional<look_reload> reload = poll_look(watch, look_poll_seconds * 0.5f);
    REQUIRE(reload.has_value());
    CHECK(near(reload->next.lighting.weather.mist, 0.6f));
    CHECK(!poll_look(watch, look_poll_seconds));

    folder.save(folder.lighting, with(misty, "\"mist\": 0.6,", "\"mist\": 7,"));
    CHECK(!poll_look(watch, look_poll_seconds));
    REQUIRE(watch.error.has_value());
    CHECK(watch.error->problem == look_problem::not_valid);
    CHECK(near(parse_lighting_from_text(watch.shown.lighting)->weather.mist, 0.6f));

    folder.save(folder.lighting, with(misty, "\"mist\": 0.6,", "\"mist\": 0.7,"));
    reload = poll_look(watch, look_poll_seconds);
    REQUIRE(reload.has_value());
    CHECK(near(reload->next.lighting.weather.mist, 0.7f));
    CHECK(!watch.error.has_value());

    folder.save(folder.picture, with(shipped_picture(), "\"target_lines\": 360", "\"target_lines\": 240"));
    reload = poll_look(watch, look_poll_seconds);
    REQUIRE(reload.has_value());
    CHECK(reload->changes.picture);
    CHECK(reload->next.picture.target_lines == 240);
}

TEST_CASE("a panel step is written to lighting.json and shown once, without the poll applying it again") {
    const look_folder folder("golfpp_test_look_panel");
    look_tuning_state state;
    look_start start = start_look(folder.root.string());
    REQUIRE(start.loaded.has_value());
    state.watch = std::move(start.watch);
    state.entries = {entry("weather/sun_glow_at_sun", 0.05)};
    state.open = true;

    look_tuning_keys right;
    right.right = true;
    const std::optional<look_reload> reload = run_look_tuning(state, right, true, fixture_palette(), 0.0f);
    REQUIRE(reload.has_value());
    CHECK(near(reload->next.lighting.weather.sun_glow_at_sun, 0.65f));
    CHECK(*read_text_file(folder.lighting) == with(fixture_lighting(), "\"sun_glow_at_sun\": 0.6,", "\"sun_glow_at_sun\": 0.65,"));
    CHECK(!state.unwritten);

    // Later polls find the step already shown, even once its write time moves.
    CHECK(!run_look_tuning(state, look_tuning_keys{}, true, reload->next.lighting, look_poll_seconds));
    folder.save(folder.lighting, *read_text_file(folder.lighting));
    CHECK(!run_look_tuning(state, look_tuning_keys{}, true, reload->next.lighting, look_poll_seconds));
    CHECK(!state.watch.error.has_value());
}
