#include "renderer/gl_loader.h"

#define GOLFPP_DEFINE_GL_FUNCTION(type, name) type golfpp_##name = nullptr;
GOLFPP_GL_FUNCTIONS(GOLFPP_DEFINE_GL_FUNCTION)
#undef GOLFPP_DEFINE_GL_FUNCTION

namespace {
template <typename function>
bool load_function(function& target, const char* name) {
    target = reinterpret_cast<function>(SDL_GL_GetProcAddress(name));
    if (target == nullptr) {
        SDL_Log("Missing GL function: %s", name);
        return false;
    }
    return true;
}
}

bool load_gl_functions() {
    bool ok = true;
#define GOLFPP_LOAD_GL_FUNCTION(type, name) ok = load_function(golfpp_##name, #name) && ok;
    GOLFPP_GL_FUNCTIONS(GOLFPP_LOAD_GL_FUNCTION)
#undef GOLFPP_LOAD_GL_FUNCTION
    return ok;
}
