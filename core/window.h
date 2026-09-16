#pragma once

#include <SDL.h>

struct window {
    // `vsync` maps to the GL swap interval. False is a profiling-only mode
    // selected at startup (see core/startup_options.h); the default is on.
    bool init(const char* title, int width, int height, bool vsync = true);
    void shutdown();
    void swap();

    SDL_Window* sdl_window() const { return window_; }

private:
    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_context_ = nullptr;
};
