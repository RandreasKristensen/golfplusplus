#pragma once

#include "game/weather.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Startup-only knobs for profiling runs, parsed once in `main()` and handed to
// `app::init`. Deliberately NOT gameplay state: nothing in `src/game/`,
// `src/physics/` or `src/renderer/` reads these, so no machine-specific
// assumption can leak into simulation or rendering behaviour.
//
// Environment variables (see docs/performance.md):
//   GOLFPP_VSYNC=0    present without waiting for vblank, so the frame time of
//                     the renderer is visible instead of a flat 16.7 ms.
//                     Anything other than a recognised "off" value keeps vsync
//                     on, which is the shipping default.
//   GOLFPP_COURSE=id  boot straight into the course with this id (for example
//                     `marienlyst_golfklub`) instead of the main menu. An id
//                     that does not exist is ignored and the menu is shown.
//   GOLFPP_WINDOW=WxH the window's starting size in pixels (3440x1440), to
//                     check the picture at other aspects and sizes.
//   GOLFPP_SCREENSHOT=path.bmp
//                     after loading, run a few frames, save the window's
//                     picture (after the TV pass) as a BMP and quit.
//   GOLFPP_GAME_TIME=hour or YYYY-MM-DDThour
//                     pin the sky at this hour on the course's clock (0 to
//                     24, e.g. 19.5) on the course's date or the given one
//                     (1989-12-21T23), for screenshots.
//   GOLFPP_WEATHER=kind[,wind[,wetness]]
//                     hold the weather offline (core/god_menu.h): a
//                     weather_kind name (clear, cloudy, light_fog, fog,
//                     light_rain, rain, heavy_rain, thunderstorm,
//                     sun_shower), the wind in
//                     m/s blowing north
//                     and the ground's wetness (0 to 1), for screenshots.
//   GOLFPP_GOD=1      the god menu (F1) can be opened, as with --god.
//   GOLFPP_SERVER=uri, GOLFPP_DB=name
//                     connect online play to another server or database
//                     than assets/play/online.json says (a local or self-hosted
//                     server). The command line wins.
//
// Command line: --server <uri>, --db <name>, and --anonymous to connect
// without signing in (servers with allow_anonymous only); --god for the god
// menu.
struct startup_options {
    bool vsync = true;
    std::string boot_course_id;
    std::string online_server;    // empty: assets/play/online.json's
    std::string online_database;  // empty: assets/play/online.json's
    bool online_anonymous = false;
    int window_width = 0;  // 0: the default size
    int window_height = 0;
    std::string screenshot_path;  // empty: no screenshot
    std::optional<float> game_hour;  // none: the sky's clock runs
    // With game_hour: days since 1970-01-01 of the game date; none: the
    // sky timeline's anchor date.
    std::optional<std::int64_t> game_date;
    // GOLFPP_WEATHER: the weather held from the start.
    std::optional<weather_kind> weather;
    std::optional<float> wind_speed;  // m/s
    std::optional<float> wetness;     // 0..1
    bool god_menu = false;
};

// Pure: no environment access, no I/O, no globals. `vsync_value` and
// `course_value` are the raw environment strings, or null when unset.
//
// vsync: null, empty or unrecognised -> true (unchanged default). Recognised
// off values, case-insensitive: "0", "off", "no", "false", "disable(d)".
// Recognised on values are accepted for symmetry but are just the default.
// course: surrounding whitespace is trimmed; whitespace-only becomes empty.
startup_options parse_startup_options(const char* vsync_value, const char* course_value);

// Adds the review switches. Pure, like parse_startup_options: the raw
// environment strings or null. The window is "WxH" with positive whole
// numbers ('x' or 'X', surrounding whitespace trimmed); anything else keeps
// the default size. The screenshot path is trimmed.
startup_options with_review_options(startup_options options, const char* window_value, const char* screenshot_value);

// Adds the pinned time of day. Pure, like parse_startup_options: the raw
// environment string or null. A number from 0 to 24, optionally after a
// "YYYY-MM-DDT" date (surrounding whitespace trimmed); anything else leaves
// the clock running.
startup_options with_game_time(startup_options options, const char* game_time_value);

// Adds the held weather and the god menu. Pure, like parse_startup_options:
// the raw environment strings or null. The weather is a kind's name, then
// optionally ",wind" (a number, 0 or more) and ",wetness" (0 to 1); anything
// else holds nothing. The god menu is on for "1", "on", "yes" or "true".
startup_options with_weather_options(startup_options options, const char* weather_value, const char* god_value);

// Adds the online options. Pure, like parse_startup_options: `server_value`
// and `database_value` are the raw environment strings or null, `arguments`
// the command line without the program name. --god turns the god menu on.
// Unknown arguments are ignored.
startup_options with_online_options(startup_options options,
                                    const char* server_value,
                                    const char* database_value,
                                    const std::vector<std::string>& arguments);
