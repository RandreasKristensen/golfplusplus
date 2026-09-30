#include "core/window.h"

#include <SDL.h>

#include "core/gl_loader.h"

namespace {
void set_window_icon(SDL_Window* window) {
    if (window == nullptr) {
        return;
    }

    SDL_Surface* icon = SDL_LoadBMP(GOLFPP_ASSETS_DIR "/icons/golfpp-icon.bmp");
    if (!icon) {
        SDL_Log("SDL_LoadBMP icon failed: %s", SDL_GetError());
        return;
    }

    SDL_SetWindowIcon(window, icon);
    SDL_FreeSurface(icon);
}
}

bool window::init(const char* title, int width, int height, const bool vsync) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    window_ = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window_) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        shutdown();
        return false;
    }

    set_window_icon(window_);

    gl_context_ = SDL_GL_CreateContext(window_);
    if (!gl_context_) {
        SDL_Log("SDL_GL_CreateContext failed: %s", SDL_GetError());
        shutdown();
        return false;
    }

    // Swap interval 0 is the profiling escape hatch: it lets the renderer run
    // past the display refresh so frame times are the renderer's, not the
    // monitor's. Nothing else in the build depends on which one is chosen.
    const int requested_swap_interval = vsync ? 1 : 0;
    if (SDL_GL_SetSwapInterval(requested_swap_interval) != 0) {
        SDL_Log("SDL_GL_SetSwapInterval(%d) failed: %s", requested_swap_interval, SDL_GetError());
    }
    SDL_Log("vsync %s (swap interval %d)", vsync ? "on" : "off (GOLFPP_VSYNC)", SDL_GL_GetSwapInterval());

    if (!load_gl_functions()) {
        SDL_Log("OpenGL loader init failed.");
        shutdown();
        return false;
    }

    const GLubyte* version = glGetString(GL_VERSION);
    if (version) {
        SDL_Log("OpenGL %s", version);
    }

    return true;
}

void window::shutdown() {
    if (gl_context_) {
        SDL_GL_DeleteContext(gl_context_);
        gl_context_ = nullptr;
    }

    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    SDL_Quit();
}

void window::swap() {
    if (window_) {
        SDL_GL_SwapWindow(window_);
    }
}
