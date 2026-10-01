#pragma once

#include <SDL.h>

#include <string>

// The SDL window and its OpenGL 3.3 core context.
class window {
public:
    // `vsync` maps to the GL swap interval (off only for profiling, see
    // core/startup_options.h). A missing icon is logged and ignored.
    bool init(const char* title, int width, int height, bool vsync, const std::string& icon_path);
    void shutdown();
    void swap();

    SDL_Window* sdl_window() const { return window_; }

private:
    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_context_ = nullptr;
};
