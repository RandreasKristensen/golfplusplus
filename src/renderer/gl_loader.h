#pragma once

// OpenGL 3.3 entry points beyond GL 1.1, loaded once through SDL after the
// context exists. OpenGL itself is global state, so this function table is the
// one allowed global in the codebase; nothing else may add globals.
//
// To use another GL function: add it to GOLFPP_GL_FUNCTIONS and add the
// matching #define below.

#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>

#define GOLFPP_GL_FUNCTIONS(X)                                      \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                      \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                        \
    X(PFNGLBEGINCONDITIONALRENDERPROC, glBeginConditionalRender)    \
    X(PFNGLBEGINQUERYPROC, glBeginQuery)                            \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                            \
    X(PFNGLBINDBUFFERBASEPROC, glBindBufferBase)                    \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                  \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                  \
    X(PFNGLBLENDEQUATIONPROC, glBlendEquation)                      \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)              \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer)                  \
    X(PFNGLBUFFERDATAPROC, glBufferData)                            \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData)                      \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)    \
    X(PFNGLCLEARBUFFERFVPROC, glClearBufferfv)                          X(PFNGLCOMPILESHADERPROC, glCompileShader)                      \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                      \
    X(PFNGLCREATESHADERPROC, glCreateShader)                        \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                      \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)            \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                      \
    X(PFNGLDELETEQUERIESPROC, glDeleteQueries)                      \
    X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers)          \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                        \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)            \
    X(PFNGLDRAWARRAYSINSTANCEDPROC, glDrawArraysInstanced)              X(PFNGLDRAWBUFFERSPROC, glDrawBuffers)                          \
    X(PFNGLDRAWELEMENTSINSTANCEDPROC, glDrawElementsInstanced)      \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)  \
    X(PFNGLGETQUERYIVPROC, glGetQueryiv)                            \
    X(PFNGLQUERYCOUNTERPROC, glQueryCounter)                        \
    X(PFNGLENDCONDITIONALRENDERPROC, glEndConditionalRender)        \
    X(PFNGLENDQUERYPROC, glEndQuery)                                \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)  \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)            X(PFNGLFRAMEBUFFERTEXTURELAYERPROC, glFramebufferTextureLayer)  \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                            \
    X(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap)                    \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                  \
    X(PFNGLGENQUERIESPROC, glGenQueries)                            \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                  \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)              \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                        \
    X(PFNGLGETQUERYOBJECTUI64VPROC, glGetQueryObjectui64v)          \
    X(PFNGLGETQUERYOBJECTUIVPROC, glGetQueryObjectuiv)              \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                          \
    X(PFNGLGETUNIFORMBLOCKINDEXPROC, glGetUniformBlockIndex)        \
    X(PFNGLGETUNIFORMIVPROC, glGetUniformiv)                        \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)            \
    X(PFNGLISPROGRAMPROC, glIsProgram)                              \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                          \
    X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage)          \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                        \
    X(PFNGLSTENCILOPSEPARATEPROC, glStencilOpSeparate)              \
    X(PFNGLTEXIMAGE3DPROC, glTexImage3D)                            \
    X(PFNGLTEXSUBIMAGE3DPROC, glTexSubImage3D)                      \
    X(PFNGLUNIFORM1FPROC, glUniform1f)                              \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                              \
    X(PFNGLUNIFORM2FVPROC, glUniform2fv)                            \
    X(PFNGLUNIFORM3FVPROC, glUniform3fv)                            \
    X(PFNGLUNIFORM4FVPROC, glUniform4fv)                            \
    X(PFNGLUNIFORMBLOCKBINDINGPROC, glUniformBlockBinding)          \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)                \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                            \
    X(PFNGLVERTEXATTRIB3FPROC, glVertexAttrib3f)                    \
    X(PFNGLVERTEXATTRIBDIVISORPROC, glVertexAttribDivisor)          \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)

#define GOLFPP_DECLARE_GL_FUNCTION(type, name) extern type golfpp_##name;
GOLFPP_GL_FUNCTIONS(GOLFPP_DECLARE_GL_FUNCTION)
#undef GOLFPP_DECLARE_GL_FUNCTION

// Logs every missing entry point; false if any is missing.
bool load_gl_functions();

#define glActiveTexture golfpp_glActiveTexture
#define glAttachShader golfpp_glAttachShader
#define glBeginConditionalRender golfpp_glBeginConditionalRender
#define glBeginQuery golfpp_glBeginQuery
#define glBindBuffer golfpp_glBindBuffer
#define glBindBufferBase golfpp_glBindBufferBase
#define glBindFramebuffer golfpp_glBindFramebuffer
#define glBindRenderbuffer golfpp_glBindRenderbuffer
#define glBindVertexArray golfpp_glBindVertexArray
#define glBlendEquation golfpp_glBlendEquation
#define glBlendFuncSeparate golfpp_glBlendFuncSeparate
#define glBlitFramebuffer golfpp_glBlitFramebuffer
#define glBufferData golfpp_glBufferData
#define glBufferSubData golfpp_glBufferSubData
#define glCheckFramebufferStatus golfpp_glCheckFramebufferStatus
#define glClearBufferfv golfpp_glClearBufferfv
#define glCompileShader golfpp_glCompileShader
#define glCreateProgram golfpp_glCreateProgram
#define glCreateShader golfpp_glCreateShader
#define glDeleteBuffers golfpp_glDeleteBuffers
#define glDeleteFramebuffers golfpp_glDeleteFramebuffers
#define glDeleteProgram golfpp_glDeleteProgram
#define glDeleteQueries golfpp_glDeleteQueries
#define glDeleteRenderbuffers golfpp_glDeleteRenderbuffers
#define glDeleteShader golfpp_glDeleteShader
#define glDeleteVertexArrays golfpp_glDeleteVertexArrays
#define glDrawArraysInstanced golfpp_glDrawArraysInstanced
#define glDrawBuffers golfpp_glDrawBuffers
#define glDrawElementsInstanced golfpp_glDrawElementsInstanced
#define glEnableVertexAttribArray golfpp_glEnableVertexAttribArray
#define glGetQueryiv golfpp_glGetQueryiv
#define glQueryCounter golfpp_glQueryCounter
#define glEndConditionalRender golfpp_glEndConditionalRender
#define glEndQuery golfpp_glEndQuery
#define glFramebufferRenderbuffer golfpp_glFramebufferRenderbuffer
#define glFramebufferTexture2D golfpp_glFramebufferTexture2D
#define glFramebufferTextureLayer golfpp_glFramebufferTextureLayer
#define glGenBuffers golfpp_glGenBuffers
#define glGenerateMipmap golfpp_glGenerateMipmap
#define glGenFramebuffers golfpp_glGenFramebuffers
#define glGenQueries golfpp_glGenQueries
#define glGenRenderbuffers golfpp_glGenRenderbuffers
#define glGenVertexArrays golfpp_glGenVertexArrays
#define glGetProgramInfoLog golfpp_glGetProgramInfoLog
#define glGetProgramiv golfpp_glGetProgramiv
#define glGetQueryObjectui64v golfpp_glGetQueryObjectui64v
#define glGetQueryObjectuiv golfpp_glGetQueryObjectuiv
#define glGetShaderInfoLog golfpp_glGetShaderInfoLog
#define glGetShaderiv golfpp_glGetShaderiv
#define glGetUniformBlockIndex golfpp_glGetUniformBlockIndex
#define glGetUniformiv golfpp_glGetUniformiv
#define glGetUniformLocation golfpp_glGetUniformLocation
#define glIsProgram golfpp_glIsProgram
#define glLinkProgram golfpp_glLinkProgram
#define glRenderbufferStorage golfpp_glRenderbufferStorage
#define glShaderSource golfpp_glShaderSource
#define glStencilOpSeparate golfpp_glStencilOpSeparate
#define glTexImage3D golfpp_glTexImage3D
#define glTexSubImage3D golfpp_glTexSubImage3D
#define glUniform1f golfpp_glUniform1f
#define glUniform1i golfpp_glUniform1i
#define glUniform2fv golfpp_glUniform2fv
#define glUniform3fv golfpp_glUniform3fv
#define glUniform4fv golfpp_glUniform4fv
#define glUniformBlockBinding golfpp_glUniformBlockBinding
#define glUniformMatrix4fv golfpp_glUniformMatrix4fv
#define glUseProgram golfpp_glUseProgram
#define glVertexAttrib3f golfpp_glVertexAttrib3f
#define glVertexAttribDivisor golfpp_glVertexAttribDivisor
#define glVertexAttribPointer golfpp_glVertexAttribPointer
