#include "core/event_loop.h"

#include "core/input.h"

#include <SDL.h>

void poll_events(input_state& input) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            input.quit_requested = true;
        }

        if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
            input.quit_requested = true;
        }
    }
}
