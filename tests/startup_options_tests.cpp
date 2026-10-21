#include "doctest.h"

#include "core/startup_options.h"
#include "game/sky_clock.h"

#include <cstdint>
#include <optional>

// `parse_startup_options` is the whole startup-option surface: main() only
// feeds it the two environment strings. These cases pin the shipping default
// (vsync on, menu boot) so a profiling switch can never change how the game
// starts for a normal player.

TEST_CASE("startup options default to vsync on and the normal menu") {
    const startup_options options = parse_startup_options(nullptr, nullptr);
    CHECK(options.vsync);
    CHECK(options.boot_course_id.empty());
}

TEST_CASE("empty environment values keep the defaults") {
    const startup_options options = parse_startup_options("", "");
    CHECK(options.vsync);
    CHECK(options.boot_course_id.empty());

    const startup_options blank = parse_startup_options("   ", "   ");
    CHECK(blank.vsync);
    CHECK(blank.boot_course_id.empty());
}

TEST_CASE("GOLFPP_VSYNC off values disable vsync") {
    CHECK(!(parse_startup_options("0", nullptr).vsync));
    CHECK(!(parse_startup_options("off", nullptr).vsync));
    CHECK(!(parse_startup_options("no", nullptr).vsync));
    CHECK(!(parse_startup_options("false", nullptr).vsync));
    CHECK(!(parse_startup_options("disable", nullptr).vsync));
    CHECK(!(parse_startup_options("disabled", nullptr).vsync));
}

TEST_CASE("GOLFPP_VSYNC off values are case and whitespace insensitive") {
    CHECK(!(parse_startup_options("OFF", nullptr).vsync));
    CHECK(!(parse_startup_options("False", nullptr).vsync));
    CHECK(!(parse_startup_options("  0  ", nullptr).vsync));
    CHECK(!(parse_startup_options("\t0\n", nullptr).vsync));
}

TEST_CASE("GOLFPP_VSYNC on values keep vsync enabled") {
    CHECK(parse_startup_options("1", nullptr).vsync);
    CHECK(parse_startup_options("on", nullptr).vsync);
    CHECK(parse_startup_options("true", nullptr).vsync);
    CHECK(parse_startup_options("YES", nullptr).vsync);
}

TEST_CASE("unrecognised GOLFPP_VSYNC values fall back to vsync on") {
    // A typo must never silently uncap the frame rate for a normal run.
    CHECK(parse_startup_options("2", nullptr).vsync);
    CHECK(parse_startup_options("banana", nullptr).vsync);
    CHECK(parse_startup_options("0 1", nullptr).vsync);
    CHECK(parse_startup_options("-0", nullptr).vsync);
}

TEST_CASE("GOLFPP_COURSE selects a course id and is trimmed") {
    CHECK(parse_startup_options(nullptr, "marienlyst_golfklub").boot_course_id == "marienlyst_golfklub");
    CHECK(parse_startup_options(nullptr, "  marienlyst_golfklub \n").boot_course_id == "marienlyst_golfklub");
    CHECK(parse_startup_options(nullptr, "course_01").boot_course_id == "course_01");
}

TEST_CASE("course ids keep their case so lookup stays exact") {
    // Course ids come from assets/world/courses/*/course.json and are matched verbatim;
    // lowercasing here would silently match the wrong course.
    CHECK(parse_startup_options(nullptr, "Marienlyst_Golfklub").boot_course_id == "Marienlyst_Golfklub");
}

TEST_CASE("the two options are independent") {
    const startup_options both = parse_startup_options("0", "marienlyst_golfklub");
    CHECK(!(both.vsync));
    CHECK(both.boot_course_id == "marienlyst_golfklub");

    const startup_options course_only = parse_startup_options(nullptr, "marienlyst_golfklub");
    CHECK(course_only.vsync);
    CHECK(course_only.boot_course_id == "marienlyst_golfklub");

    const startup_options vsync_only = parse_startup_options("0", nullptr);
    CHECK(!(vsync_only.vsync));
    CHECK(vsync_only.boot_course_id.empty());
}

TEST_CASE("online options come from the environment, and the command line wins") {
    const startup_options none = with_online_options(startup_options{}, nullptr, nullptr, {});
    CHECK(none.online_server.empty());
    CHECK(none.online_database.empty());
    CHECK(!none.online_anonymous);

    const startup_options environment = with_online_options(startup_options{}, " http://localhost:3000 ", "dev", {});
    CHECK(environment.online_server == "http://localhost:3000");
    CHECK(environment.online_database == "dev");

    const startup_options command_line = with_online_options(
        startup_options{}, "http://env", "env_db", {"--server", "http://cli", "--anonymous", "--db", "cli_db", "--stray"});
    CHECK(command_line.online_server == "http://cli");
    CHECK(command_line.online_database == "cli_db");
    CHECK(command_line.online_anonymous);

    // A flag missing its value is ignored.
    CHECK(with_online_options(startup_options{}, "http://env", nullptr, {"--server"}).online_server == "http://env");
}

TEST_CASE("GOLFPP_GAME_TIME pins the time of day to an hour from 0 to 24") {
    CHECK(!with_game_time(startup_options{}, nullptr).game_hour.has_value());
    CHECK(!with_game_time(startup_options{}, "  ").game_hour.has_value());
    REQUIRE(with_game_time(startup_options{}, " 19.5 ").game_hour.has_value());
    CHECK(*with_game_time(startup_options{}, " 19.5 ").game_hour == 19.5f);
    CHECK(*with_game_time(startup_options{}, "0").game_hour == 0.0f);
    CHECK(*with_game_time(startup_options{}, "24").game_hour == 24.0f);
    for (const char* bad : {"25", "-1", "noon", "12h", "1 2"}) {
        CHECK(!with_game_time(startup_options{}, bad).game_hour.has_value());
    }
    CHECK(!with_game_time(startup_options{}, "19.5").game_date.has_value());
}

TEST_CASE("GOLFPP_GAME_TIME can pin the hour on a game date of its own") {
    const startup_options winter = with_game_time(startup_options{}, " 1989-12-21T23.5 ");
    REQUIRE(winter.game_hour.has_value());
    CHECK(*winter.game_hour == 23.5f);
    CHECK(winter.game_date == std::optional<std::int64_t>(days_from_civil(1989, 12, 21)));
    for (const char* bad : {"1989-12-21T", "1989-12-21T25", "1989-13-21T12", "1989-12-21 12", "1989-12-21T 12", "89-12-21T12",
                            "1989-12-21Tnoon"}) {
        const startup_options options = with_game_time(startup_options{}, bad);
        CHECK(!options.game_hour.has_value());
        CHECK(!options.game_date.has_value());
    }
}

TEST_CASE("GOLFPP_WINDOW sets the window size and GOLFPP_SCREENSHOT the picture's path") {
    const startup_options none = with_review_options(startup_options{}, nullptr, nullptr);
    CHECK(none.window_width == 0);
    CHECK(none.window_height == 0);
    CHECK(none.screenshot_path.empty());

    const startup_options sized = with_review_options(startup_options{}, " 3440x1440 ", " shots/wide.bmp\n");
    CHECK(sized.window_width == 3440);
    CHECK(sized.window_height == 1440);
    CHECK(sized.screenshot_path == "shots/wide.bmp");
    CHECK(with_review_options(startup_options{}, "1024X768", nullptr).window_width == 1024);

    // Anything but two positive whole numbers keeps the default size.
    for (const char* bad : {"", "1920", "1920x", "x1080", "0x1080", "1920x0", "-5x10", "19.5x10", "1920 x 1080", "axb",
                            "999999x10"}) {
        const startup_options options = with_review_options(startup_options{}, bad, nullptr);
        CHECK(options.window_width == 0);
        CHECK(options.window_height == 0);
    }
}

TEST_CASE("GOLFPP_WEATHER holds a weather kind, optionally with wind and wetness, and GOLFPP_GOD or --god opens the god menu") {
    CHECK(!with_weather_options(startup_options{}, nullptr, nullptr).weather.has_value());
    CHECK(!with_weather_options(startup_options{}, nullptr, nullptr).god_menu);
    const startup_options rain = with_weather_options(startup_options{}, " heavy_rain,8,1 ", "1");
    REQUIRE(rain.weather.has_value());
    CHECK(*rain.weather == weather_kind::heavy_rain);
    CHECK(rain.wind_speed == std::optional<float>(8.0f));
    CHECK(rain.wetness == std::optional<float>(1.0f));
    CHECK(rain.god_menu);
    const startup_options shower = with_weather_options(startup_options{}, "sun_shower", "0");
    CHECK(shower.weather == std::optional<weather_kind>(weather_kind::sun_shower));
    CHECK(!shower.wind_speed.has_value());
    CHECK(!shower.god_menu);
    for (const char* bad : {"snow", "rain,", "rain,-1", "rain,3,2", "rain,3,0.5,1", "rain, 3"}) {
        CHECK(!with_weather_options(startup_options{}, bad, nullptr).weather.has_value());
    }
    CHECK(with_online_options(startup_options{}, nullptr, nullptr, {"--god"}).god_menu);
}
