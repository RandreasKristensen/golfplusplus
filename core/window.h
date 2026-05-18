#pragma once

#include <SDL.h>

struct window {
    bool init(const char* title, int width, int height);
    void shutdown();
    void swap();

    SDL_Window* sdl_window() const { return window_; }

private:
    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_context_ = nullptr;
};
