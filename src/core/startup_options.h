#pragma once

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
//   GOLFPP_SERVER=uri, GOLFPP_DB=name
//                     connect online play to another server or database
//                     than assets/online.json says (a local or self-hosted
//                     server). The command line wins.
//
// Command line: --server <uri>, --db <name>, and --anonymous to connect
// without signing in (servers with allow_anonymous only).
struct startup_options {
    bool vsync = true;
    std::string boot_course_id;
    std::string online_server;    // empty: assets/online.json's
    std::string online_database;  // empty: assets/online.json's
    bool online_anonymous = false;
};

// Pure: no environment access, no I/O, no globals. `vsync_value` and
// `course_value` are the raw environment strings, or null when unset.
//
// vsync: null, empty or unrecognised -> true (unchanged default). Recognised
// off values, case-insensitive: "0", "off", "no", "false", "disable(d)".
// Recognised on values are accepted for symmetry but are just the default.
// course: surrounding whitespace is trimmed; whitespace-only becomes empty.
startup_options parse_startup_options(const char* vsync_value, const char* course_value);

// Adds the online options. Pure, like parse_startup_options: `server_value`
// and `database_value` are the raw environment strings or null, `arguments`
// the command line without the program name. Unknown arguments are ignored.
startup_options with_online_options(startup_options options,
                                    const char* server_value,
                                    const char* database_value,
                                    const std::vector<std::string>& arguments);
