#include "doctest.h"

#include "core/progress_saver.h"
#include "game/content_files.h"
#include "game/hub_flow.h"
#include "game/net_types.h"
#include "game/save_data.h"
#include "game/save_manager.h"
#include "game/settings.h"
#include "test_support.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

namespace {
// A scratch directory holding one save at its real relative path.
struct save_fixture {
    std::filesystem::path root;
    std::filesystem::path path;
    explicit save_fixture(const std::string& name)
        : root(std::filesystem::temp_directory_path() / ("golfpp_test_" + name)), path(save_file_path(root)) {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        std::filesystem::create_directories(path.parent_path(), error);
    }
    ~save_fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

std::string save_with_xp(const std::int64_t xp) {
    save_data save;
    save.skills["golf_swing"].xp = xp;
    return save_data_to_json(save);
}

void put(const std::filesystem::path& file, const std::string& text) {
    std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
}

std::int64_t loaded_xp(const std::filesystem::path& path) {
    return skill_xp(load_save(path).save.skills, "golf_swing");
}

bool any_unreadable_copy(const std::filesystem::path& path) {
    for (const auto& entry : std::filesystem::directory_iterator(path.parent_path())) {
        if (entry.path().filename().string().find(".unreadable-") != std::string::npos) {
            return true;
        }
    }
    return false;
}
}

TEST_CASE("a write interrupted after the old save moved aside is recovered from the complete temp file") {
    save_fixture fixture("recover_after_backup");
    put(replacement_backup_path(fixture.path), save_with_xp(10));
    put(replacement_temp_path(fixture.path), save_with_xp(20));

    const save_load_result result = load_save(fixture.path);
    CHECK(result.loaded_existing);
    CHECK(result.recovered_from_interrupted_write);
    CHECK(!result.existing_was_unreadable);
    CHECK(skill_xp(result.save.skills, "golf_swing") == 20);
    CHECK(std::filesystem::exists(fixture.path));
    CHECK(!std::filesystem::exists(replacement_temp_path(fixture.path)));
    CHECK(loaded_xp(fixture.path) == 20);
}

TEST_CASE("an interrupted write never replaces a valid primary or fakes a fresh save") {
    save_fixture fixture("recover_precedence");

    // Crash before the old file moved aside: the temp file may be half written.
    put(fixture.path, save_with_xp(10));
    put(replacement_temp_path(fixture.path), "{ \"skills\": {\"golf_sw");
    const save_load_result result = load_save(fixture.path);
    CHECK(skill_xp(result.save.skills, "golf_swing") == 10);
    CHECK(!result.recovered_from_interrupted_write);
    CHECK(!result.existing_was_unreadable);
    CHECK(!any_unreadable_copy(fixture.path));

    // Crash after the new file was installed: the backup is the stale one.
    put(fixture.path, save_with_xp(30));
    put(replacement_backup_path(fixture.path), save_with_xp(10));
    put(replacement_temp_path(fixture.path), save_with_xp(20));
    CHECK(loaded_xp(fixture.path) == 30);
}

TEST_CASE("only a backup left behind is recovered, and a half-written temp file is kept as evidence") {
    save_fixture fixture("recover_backup_only");
    put(replacement_backup_path(fixture.path), save_with_xp(10));

    save_load_result result = load_save(fixture.path);
    CHECK(result.recovered_from_interrupted_write);
    CHECK(skill_xp(result.save.skills, "golf_swing") == 10);
    CHECK(std::filesystem::exists(fixture.path));

    std::filesystem::remove(fixture.path);
    put(replacement_temp_path(fixture.path), "{ \"skills\":");
    put(replacement_backup_path(fixture.path), save_with_xp(10));
    result = load_save(fixture.path);
    CHECK(result.recovered_from_interrupted_write);
    CHECK(skill_xp(result.save.skills, "golf_swing") == 10);
    CHECK(result.existing_was_unreadable);
    CHECK(any_unreadable_copy(fixture.path));
}

TEST_CASE("a corrupt primary is kept as evidence while a complete leftover is recovered") {
    save_fixture fixture("recover_corrupt_primary");
    put(fixture.path, "{ corrupt");
    put(replacement_backup_path(fixture.path), save_with_xp(10));
    put(replacement_temp_path(fixture.path), save_with_xp(20));

    const save_load_result result = load_save(fixture.path);
    CHECK(result.loaded_existing);
    CHECK(skill_xp(result.save.skills, "golf_swing") == 20);
    CHECK(result.existing_was_unreadable);
    REQUIRE(!result.unreadable_backup.empty());
    CHECK(*read_text_file(result.unreadable_backup) == "{ corrupt");
}

TEST_CASE("when no candidate is readable the fresh save keeps every file as evidence") {
    save_fixture fixture("recover_nothing_readable");
    put(fixture.path, "{ corrupt");
    put(replacement_backup_path(fixture.path), "also corrupt");

    const save_load_result result = load_save(fixture.path);
    CHECK(!result.loaded_existing);
    CHECK(result.existing_was_unreadable);
    CHECK(!result.unreadable_backup.empty());
    CHECK(std::filesystem::exists(fixture.path));
    CHECK(std::filesystem::exists(replacement_backup_path(fixture.path)));

    save_fixture absent("recover_absent");
    const save_load_result none = load_save(absent.path);
    CHECK(!none.loaded_existing);
    CHECK(!none.existing_was_unreadable);
    CHECK(!none.recovered_from_interrupted_write);
}

TEST_CASE("a save that cannot move the old file aside fails and leaves the previous save intact") {
    save_fixture fixture("failed_backup");
    save_data save;
    save.skills["golf_swing"].xp = 10;
    REQUIRE(write_save(fixture.path, save));
    // A non-empty directory where the backup belongs makes the first rename fail.
    const std::filesystem::path backup = replacement_backup_path(fixture.path);
    std::filesystem::create_directories(backup / "blocker");
    put(backup / "blocker" / "file", "x");

    save.skills["golf_swing"].xp = 20;
    CHECK(!write_save(fixture.path, save));
    CHECK(loaded_xp(fixture.path) == 10);
    CHECK(!std::filesystem::exists(replacement_temp_path(fixture.path)));
}

TEST_CASE("settings interrupted mid replacement come back from the complete temp file") {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "golfpp_test_settings_recovery";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    const std::filesystem::path path = settings_file_path(root);
    std::filesystem::create_directories(root, error);
    const std::vector<setting_definition> definitions = fixture_content().settings;
    REQUIRE(!definitions.empty());
    settings_values values;
    values[definitions.front().id] = definitions.front().max;
    REQUIRE(write_settings(path, values, definitions));
    std::filesystem::rename(path, replacement_backup_path(path), error);
    REQUIRE(!error);
    put(replacement_temp_path(path), settings_to_text(values, definitions));

    CHECK(load_settings(path, definitions) == values);
    CHECK(std::filesystem::exists(path));
    std::filesystem::remove_all(root, error);
}

TEST_CASE("save xp outside the stored range is refused, and the largest valid xp round-trips") {
    constexpr std::int64_t most = std::numeric_limits<std::int64_t>::max();
    const auto xp_text = [](const std::string& value) {
        return R"({"version": 9, "skills": {"golf_swing": )" + value + "}}";
    };
    const std::optional<save_data> largest = parse_save_data(xp_text(std::to_string(most)));
    REQUIRE(largest.has_value());
    CHECK(skill_xp(largest->skills, "golf_swing") == most);
    const std::optional<save_data> again = parse_save_data(save_data_to_json(*largest));
    REQUIRE(again.has_value());
    CHECK(skill_xp(again->skills, "golf_swing") == most);

    CHECK(!parse_save_data(xp_text("9223372036854775808")));    // would wrap to INT64_MIN
    CHECK(!parse_save_data(xp_text("18446744073709551615")));   // would wrap to -1
    CHECK(!parse_save_data(xp_text("-1")));
    CHECK(!parse_save_data(xp_text("1.5")));
    CHECK(!parse_save_data(xp_text("\"7\"")));

    save_fixture fixture("xp_out_of_range");
    put(fixture.path, xp_text("9223372036854775808"));
    const save_load_result result = load_save(fixture.path);
    CHECK(!result.loaded_existing);
    CHECK(result.existing_was_unreadable);
    REQUIRE(!result.unreadable_backup.empty());
    CHECK(*read_text_file(result.unreadable_backup) == xp_text("9223372036854775808"));
}

TEST_CASE("the bus from a course to the hub saves offline progress, and online never writes") {
    const auto bus_to_hub = [](const play_mode play) -> game_state {
        game_state game = started_game(fixture_hub_course());
        game.play = play;
        if (play == play_mode::online) {
            game.online.account_id = 1;
            game.online.room = online_room{7, online_hub_course, 5, 1};
            game.online.room_ready = true;
        }
        game.save.skills["golf_swing"].xp = 40;
        request_bus_travel(game);
        CHECK(game.hub_flow.travel_course == online_hub_course);
        return game;
    };

    save_fixture offline("bus_offline");
    game_state game = bus_to_hub(play_mode::offline);
    CHECK(!std::filesystem::exists(offline.path));
    REQUIRE(enter_place_saving(game, online_hub_course, progress_saver{offline.path, true}));
    CHECK(game.kind == place_kind::hub);
    CHECK(loaded_xp(offline.path) == 40);

    // A failed arrival is not a confirmed transition.
    save_fixture failed("bus_failed");
    game = bus_to_hub(play_mode::offline);
    CHECK(!enter_place_saving(game, "no_such_place", progress_saver{failed.path, true}));
    CHECK(!std::filesystem::exists(failed.path));

    save_fixture disabled("bus_disabled");
    game = bus_to_hub(play_mode::offline);
    REQUIRE(enter_place_saving(game, online_hub_course, progress_saver{disabled.path, false}));
    CHECK(!std::filesystem::exists(disabled.path));

    save_fixture online("bus_online");
    game = bus_to_hub(play_mode::online);
    REQUIRE(enter_place_saving(game, online_hub_course, progress_saver{online.path, true}));
    CHECK(game.kind == place_kind::hub);
    CHECK(!std::filesystem::exists(online.path));

    // Leaving the hub for a course is not leaving a round.
    save_fixture outbound("bus_outbound");
    game = make_game_state(fixture_content(), save_data{});
    REQUIRE(enter_place(game, online_hub_course));
    game.hub_flow.travel_course = fixture_hub_course().id;
    REQUIRE(enter_place_saving(game, fixture_hub_course().id, progress_saver{outbound.path, true}));
    CHECK(!std::filesystem::exists(outbound.path));
}
