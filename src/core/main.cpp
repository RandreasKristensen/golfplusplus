#include "core/app.h"
#include "core/startup_options.h"

#include <SDL.h>

#include <string>
#include <vector>

int main(int argc, char** argv) {
    // The only place the environment and command line are read. Parsing
    // itself is pure and unit tested; see core/startup_options.h and
    // docs/performance.md.
    const std::vector<std::string> arguments(argv + (argc > 0 ? 1 : 0), argv + argc);
    const startup_options options =
        with_online_options(parse_startup_options(SDL_getenv("GOLFPP_VSYNC"), SDL_getenv("GOLFPP_COURSE")),
                            SDL_getenv("GOLFPP_SERVER"), SDL_getenv("GOLFPP_DB"), arguments);

    app game;
    if (!game.init(options)) {
        return 1;
    }

    game.run();
    game.shutdown();
    return 0;
}
