#include "core/gl_loader.h"

#if defined(VCR_GOLF_USE_GLAD)
bool load_gl_functions() {
    return gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)) != 0;
}
#else
PFNGLGENVERTEXARRAYSPROC glGenVertexArrays_ptr = nullptr;
PFNGLBINDVERTEXARRAYPROC glBindVertexArray_ptr = nullptr;
PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays_ptr = nullptr;
PFNGLGENBUFFERSPROC glGenBuffers_ptr = nullptr;
PFNGLBINDBUFFERPROC glBindBuffer_ptr = nullptr;
PFNGLBUFFERDATAPROC glBufferData_ptr = nullptr;
PFNGLDELETEBUFFERSPROC glDeleteBuffers_ptr = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray_ptr = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer_ptr = nullptr;
PFNGLACTIVETEXTUREPROC glActiveTexture_ptr = nullptr;

PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers_ptr = nullptr;
PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer_ptr = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D_ptr = nullptr;
PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers_ptr = nullptr;
PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer_ptr = nullptr;
PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage_ptr = nullptr;
PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer_ptr = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus_ptr = nullptr;
PFNGLDELETERENDERBUFFERSPROC glDeleteRenderbuffers_ptr = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers_ptr = nullptr;

PFNGLCREATESHADERPROC glCreateShader_ptr = nullptr;
PFNGLSHADERSOURCEPROC glShaderSource_ptr = nullptr;
PFNGLCOMPILESHADERPROC glCompileShader_ptr = nullptr;
PFNGLGETSHADERIVPROC glGetShaderiv_ptr = nullptr;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog_ptr = nullptr;
PFNGLDELETESHADERPROC glDeleteShader_ptr = nullptr;
PFNGLCREATEPROGRAMPROC glCreateProgram_ptr = nullptr;
PFNGLATTACHSHADERPROC glAttachShader_ptr = nullptr;
PFNGLLINKPROGRAMPROC glLinkProgram_ptr = nullptr;
PFNGLGETPROGRAMIVPROC glGetProgramiv_ptr = nullptr;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog_ptr = nullptr;
PFNGLDELETEPROGRAMPROC glDeleteProgram_ptr = nullptr;
PFNGLUSEPROGRAMPROC glUseProgram_ptr = nullptr;

PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation_ptr = nullptr;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv_ptr = nullptr;
PFNGLUNIFORM3FVPROC glUniform3fv_ptr = nullptr;
PFNGLUNIFORM2FVPROC glUniform2fv_ptr = nullptr;
PFNGLUNIFORM1FPROC glUniform1f_ptr = nullptr;
PFNGLUNIFORM1IPROC glUniform1i_ptr = nullptr;

namespace {
template <typename T>
bool load_proc(T& proc, const char* name) {
    proc = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    if (!proc) {
        SDL_Log("Missing GL function: %s", name);
        return false;
    }
    return true;
}
}

bool load_gl_functions() {
    bool ok = true;

    ok &= load_proc(glGenVertexArrays_ptr, "glGenVertexArrays");
    ok &= load_proc(glBindVertexArray_ptr, "glBindVertexArray");
    ok &= load_proc(glDeleteVertexArrays_ptr, "glDeleteVertexArrays");
    ok &= load_proc(glGenBuffers_ptr, "glGenBuffers");
    ok &= load_proc(glBindBuffer_ptr, "glBindBuffer");
    ok &= load_proc(glBufferData_ptr, "glBufferData");
    ok &= load_proc(glDeleteBuffers_ptr, "glDeleteBuffers");
    ok &= load_proc(glEnableVertexAttribArray_ptr, "glEnableVertexAttribArray");
    ok &= load_proc(glVertexAttribPointer_ptr, "glVertexAttribPointer");
    ok &= load_proc(glActiveTexture_ptr, "glActiveTexture");

    ok &= load_proc(glGenFramebuffers_ptr, "glGenFramebuffers");
    ok &= load_proc(glBindFramebuffer_ptr, "glBindFramebuffer");
    ok &= load_proc(glFramebufferTexture2D_ptr, "glFramebufferTexture2D");
    ok &= load_proc(glGenRenderbuffers_ptr, "glGenRenderbuffers");
    ok &= load_proc(glBindRenderbuffer_ptr, "glBindRenderbuffer");
    ok &= load_proc(glRenderbufferStorage_ptr, "glRenderbufferStorage");
    ok &= load_proc(glFramebufferRenderbuffer_ptr, "glFramebufferRenderbuffer");
    ok &= load_proc(glCheckFramebufferStatus_ptr, "glCheckFramebufferStatus");
    ok &= load_proc(glDeleteRenderbuffers_ptr, "glDeleteRenderbuffers");
    ok &= load_proc(glDeleteFramebuffers_ptr, "glDeleteFramebuffers");

    ok &= load_proc(glCreateShader_ptr, "glCreateShader");
    ok &= load_proc(glShaderSource_ptr, "glShaderSource");
    ok &= load_proc(glCompileShader_ptr, "glCompileShader");
    ok &= load_proc(glGetShaderiv_ptr, "glGetShaderiv");
    ok &= load_proc(glGetShaderInfoLog_ptr, "glGetShaderInfoLog");
    ok &= load_proc(glDeleteShader_ptr, "glDeleteShader");
    ok &= load_proc(glCreateProgram_ptr, "glCreateProgram");
    ok &= load_proc(glAttachShader_ptr, "glAttachShader");
    ok &= load_proc(glLinkProgram_ptr, "glLinkProgram");
    ok &= load_proc(glGetProgramiv_ptr, "glGetProgramiv");
    ok &= load_proc(glGetProgramInfoLog_ptr, "glGetProgramInfoLog");
    ok &= load_proc(glDeleteProgram_ptr, "glDeleteProgram");
    ok &= load_proc(glUseProgram_ptr, "glUseProgram");

    ok &= load_proc(glGetUniformLocation_ptr, "glGetUniformLocation");
    ok &= load_proc(glUniformMatrix4fv_ptr, "glUniformMatrix4fv");
    ok &= load_proc(glUniform3fv_ptr, "glUniform3fv");
    ok &= load_proc(glUniform2fv_ptr, "glUniform2fv");
    ok &= load_proc(glUniform1f_ptr, "glUniform1f");
    ok &= load_proc(glUniform1i_ptr, "glUniform1i");

    return ok;
}
#endif
