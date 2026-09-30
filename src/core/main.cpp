#include "core/app.h"
#include "core/startup_options.h"

#include <SDL.h>

int main(int, char**) {
    // The only place the environment is read. Parsing itself is pure and unit
    // tested; see core/startup_options.h and docs/performance.md.
    const startup_options options = parse_startup_options(SDL_getenv("GOLFPP_VSYNC"),
                                                          SDL_getenv("GOLFPP_COURSE"));

    app game;
    if (!game.init(options)) {
        return 1;
    }

    game.run();
    game.shutdown();
    return 0;
}
