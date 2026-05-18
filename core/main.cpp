#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "core/app.h"

int main(int, char**) {
    app game;
    if (!game.init()) {
        return 1;
    }

    game.run();
    game.shutdown();
    return 0;
}
