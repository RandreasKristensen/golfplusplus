#pragma once

#include <SDL.h>

// Looks up a GL entry point that core/gl_loader does not provide (instancing,
// timer queries, glBufferSubData, ...) in the current context. Callers keep
// the result in a member, never in a global, and cast it to the typed
// function pointer at the call site. Returns null when unavailable.
inline void* load_gl_proc(const char* name) {
    return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
}
