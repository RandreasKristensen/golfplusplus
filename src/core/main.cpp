#include "core/app.h"
#include "core/startup_options.h"

#include <SDL.h>

#include <optional>
#include <string>
#include <vector>

namespace {
// SDL_getenv may return the same buffer on every call (it does on Windows),
// so each value is copied before the next is read.
struct environment_value {
    std::optional<std::string> value;
    explicit environment_value(const char* name) {
        if (const char* raw = SDL_getenv(name)) {
            value = raw;
        }
    }
    const char* c_str() const { return value ? value->c_str() : nullptr; }
};
}

int main(int argc, char** argv) {
    // The only place the environment and command line are read. Parsing
    // itself is pure and unit tested; see core/startup_options.h and
    // docs/performance.md.
    const std::vector<std::string> arguments(argv + (argc > 0 ? 1 : 0), argv + argc);
    const environment_value vsync("GOLFPP_VSYNC");
    const environment_value course("GOLFPP_COURSE");
    const environment_value window("GOLFPP_WINDOW");
    const environment_value screenshot("GOLFPP_SCREENSHOT");
    const environment_value scene_resolution("GOLFPP_SCENE_RESOLUTION");
    const environment_value game_time("GOLFPP_GAME_TIME");
    const environment_value server("GOLFPP_SERVER");
    const environment_value database("GOLFPP_DB");
    const environment_value weather("GOLFPP_WEATHER");
    const environment_value god("GOLFPP_GOD");
    const environment_value tour("GOLFPP_TOUR");
    const environment_value benchmark("GOLFPP_BENCHMARK");
    const environment_value duration("GOLFPP_BENCHMARK_MS");
    const environment_value reverse("GOLFPP_BENCHMARK_REVERSE");
    const startup_options startup = with_online_options(
        with_tour_option(
            with_weather_options(
                with_game_time(
                    with_review_options(parse_startup_options(vsync.c_str(), course.c_str()), window.c_str(), screenshot.c_str(),
                                        scene_resolution.c_str()),
                    game_time.c_str()),
                weather.c_str(), god.c_str()),
            tour.c_str()),
        server.c_str(), database.c_str(), arguments);

    const auto options = with_benchmark_options(startup, benchmark.c_str(), duration.c_str(), reverse.c_str());
    app game;
    if (!game.init(options)) {
        return 1;
    }

    const bool ok = options.benchmark_directory.empty() ? (game.run(), true) : game.run_benchmark(options);
    game.shutdown();
    return ok ? 0 : 1;
}
