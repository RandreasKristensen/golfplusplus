#include "core/event_loop.h"

#include <SDL.h>

namespace {
button_state* button_for_scancode(input_state& input, const SDL_Scancode scancode) {
    switch (scancode) {
    case SDL_SCANCODE_UP:
        return &input.up;
    case SDL_SCANCODE_DOWN:
        return &input.down;
    case SDL_SCANCODE_LEFT:
        return &input.left;
    case SDL_SCANCODE_RIGHT:
        return &input.right;
    case SDL_SCANCODE_SPACE:
        return &input.space;
    case SDL_SCANCODE_LSHIFT:
        return &input.left_shift;
    case SDL_SCANCODE_RSHIFT:
        return &input.right_shift;
    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL:
        return &input.ctrl;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        return &input.enter;
    case SDL_SCANCODE_TAB:
        return &input.tab;
    case SDL_SCANCODE_CAPSLOCK:
        return &input.caps_lock;
    case SDL_SCANCODE_BACKSPACE:
        return &input.backspace;
    case SDL_SCANCODE_ESCAPE:
        return &input.escape;
    case SDL_SCANCODE_R:
        return &input.key_r;
    case SDL_SCANCODE_1:
        return &input.key_1;
    case SDL_SCANCODE_2:
        return &input.key_2;
    default:
        return nullptr;
    }
}

void set_button(button_state& button, const bool down) {
    if (down && !button.is_down) {
        button.pressed = true;
    }
    if (!down && button.is_down) {
        button.released = true;
    }
    button.is_down = down;
}
}

input_state poll_events(const input_state& previous) {
    input_state input = next_frame_input(previous);
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            input.quit_requested = true;
            break;
        case SDL_TEXTINPUT:
            input.text_typed += event.text.text;
            break;
        case SDL_MOUSEMOTION:
            input.mouse_x = event.motion.x;
            input.mouse_y = event.motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                input.mouse_x = event.button.x;
                input.mouse_y = event.button.y;
                set_button(input.mouse_left, event.type == SDL_MOUSEBUTTONDOWN);
            }
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            if (event.key.repeat == 0) {
                if (button_state* button = button_for_scancode(input, event.key.keysym.scancode)) {
                    set_button(*button, event.type == SDL_KEYDOWN);
                }
            }
            break;
        default:
            break;
        }
    }
    return input;
}

void set_text_input_enabled(const bool enabled) {
    if (enabled && !SDL_IsTextInputActive()) {
        SDL_StartTextInput();
    } else if (!enabled && SDL_IsTextInputActive()) {
        SDL_StopTextInput();
    }
}
