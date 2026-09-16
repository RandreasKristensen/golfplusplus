#include "doctest.h"

#include "core/startup_options.h"

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
    // Course ids come from assets/courses/*.json and are matched verbatim;
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
