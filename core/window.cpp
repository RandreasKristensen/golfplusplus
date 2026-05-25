#include "core/window.h"

#include <SDL.h>

#include "core/gl_loader.h"

namespace {
void set_window_icon(SDL_Window* window) {
    if (window == nullptr) {
        return;
    }

    SDL_Surface* icon = SDL_LoadBMP(VCR_GOLF_ASSETS_DIR "/icons/golfpp-icon.bmp");
    if (!icon) {
        SDL_Log("SDL_LoadBMP icon failed: %s", SDL_GetError());
        return;
    }

    SDL_SetWindowIcon(window, icon);
    SDL_FreeSurface(icon);
}
}

bool window::init(const char* title, int width, int height) {
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
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN
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

    SDL_GL_SetSwapInterval(1);

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
