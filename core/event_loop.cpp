#include "core/event_loop.h"

#include "core/input.h"

#include <SDL.h>

namespace {
button_state* button_for_scancode(input_state& input, const SDL_Scancode scancode) {
    switch (scancode) {
    case SDL_SCANCODE_LEFT:
        return &input.left;
    case SDL_SCANCODE_RIGHT:
        return &input.right;
    case SDL_SCANCODE_UP:
        return &input.up;
    case SDL_SCANCODE_DOWN:
        return &input.down;
    case SDL_SCANCODE_SPACE:
        return &input.space;
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT:
        return &input.shift;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        return &input.enter;
    case SDL_SCANCODE_BACKSPACE:
        return &input.backspace;
    case SDL_SCANCODE_R:
        return &input.retee;
    case SDL_SCANCODE_ESCAPE:
        return &input.escape;
    default:
        return nullptr;
    }
}

void set_button_down(button_state& button) {
    if (!button.is_down) {
        button.pressed = true;
    }
    button.is_down = true;
}

void set_button_up(button_state& button) {
    if (button.is_down) {
        button.released = true;
    }
    button.is_down = false;
}
}

void poll_events(input_state& input) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            input.quit_requested = true;
        }

        if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
            button_state* button = button_for_scancode(input, event.key.keysym.scancode);
            if (button != nullptr) {
                set_button_down(*button);
            }

        }

        if (event.type == SDL_KEYUP && event.key.repeat == 0) {
            button_state* button = button_for_scancode(input, event.key.keysym.scancode);
            if (button != nullptr) {
                set_button_up(*button);
            }
        }
    }
}
