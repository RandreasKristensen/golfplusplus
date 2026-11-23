#include "renderer/gl_storage.h"
#include "renderer/gl_loader.h"
#include "profiling/profiling.h"

#include <algorithm>

namespace {
unsigned texel_bytes(const unsigned format) {
    switch (format) {
    case GL_RGB:
    case GL_DEPTH_COMPONENT24: return 3;
    case GL_R8:
    case GL_R8_SNORM: return 1;
    case GL_R16:
    case GL_R16UI: return 2;
    case GL_R32F: return 4;
    case GL_RGB32F: return 12;
    case GL_RGBA16F: return 8;
    case GL_RGBA32F: return 16;
    case GL_DEPTH24_STENCIL8:
    case GL_RGBA:
    case GL_RGBA8:
    case GL_R11F_G11F_B10F: return 4;
    }
    SDL_Log("gl_storage: no byte size for internal format 0x%04X; logical GPU bytes under-report", format);
    return 0;
}

void transfer(frame_profile* frame, const upload_kind kind, const std::size_t bytes) {
    if (!frame || !bytes) { return; }
    if (kind == upload_kind::cached) {
        ++frame->work.static_uploads;
        frame->work.static_upload_bytes += bytes;
    } else {
        frame->work.dynamic_upload_bytes += bytes;
    }
}
}

void gl_storage::buffer(const unsigned id, const unsigned target, const std::size_t bytes, const void* data,
                        const unsigned usage, frame_profile* standalone) const {
    auto* frame = profile(standalone);
    {
        const profile_scope timer(frame, profile_stage::upload);
        glBufferData(target, static_cast<GLsizeiptr>(bytes), data, usage);
    }
    if (ledger && ledger->buffers.replace(id, bytes) && frame) { ++frame->work.buffer_growths; }
    record_buffer_upload(frame, data ? bytes : 0);
    if (data) { transfer(frame, upload_kind::cached, bytes); }
}

void gl_storage::write(const unsigned target, const std::size_t offset, const std::size_t bytes,
                       const void* data, const upload_kind kind, frame_profile* standalone) const {
    auto* frame = profile(standalone);
    {
        const profile_scope timer(frame, profile_stage::upload);
        glBufferSubData(target, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(bytes), data);
    }
    record_buffer_write(frame, bytes);
    transfer(frame, kind, bytes);
}

void gl_storage::texture_image(const unsigned id, const unsigned internal_format, const int width, const int height,
                               const unsigned format, const unsigned type, const void* data, const bool mipmaps) const {
    {
        const profile_scope timer(profile(), profile_stage::upload);
        glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, format, type, data);
        if (mipmaps) { glGenerateMipmap(GL_TEXTURE_2D); }
    }
    if (ledger) {
        const auto stride = texel_bytes(internal_format);
        ledger->textures.replace(id, texture_storage_bytes(width, height, stride, mipmaps));
        if (profile()) { ++profile()->work.texture_allocations; }
        if (data) { transfer(profile(), upload_kind::cached, texture_storage_bytes(width, height, stride, false)); }
    }
}

void gl_storage::texture_array(const unsigned id, const unsigned internal_format, const int width, const int height,
                               const int layers, const unsigned format, const unsigned type, const bool mipmaps) const {
    {
        const profile_scope timer(profile(), profile_stage::upload);
        int levels = 1;
        for (int side = std::max(width, height); mipmaps && side > 1; side /= 2) { ++levels; }
        for (int level = 0; level < levels; ++level) {
            glTexImage3D(GL_TEXTURE_2D_ARRAY, level, static_cast<GLint>(internal_format), std::max(width >> level, 1),
                         std::max(height >> level, 1), layers, 0, format, type, nullptr);
        }
    }
    if (ledger) {
        ledger->textures.replace(id, texture_storage_bytes(width, height, texel_bytes(internal_format), mipmaps) * static_cast<std::uint64_t>(layers));
        if (profile()) { ++profile()->work.texture_allocations; }
    }
}

void gl_storage::texture_layer(const unsigned format, const unsigned type, const int width, const int height,
                               const int layer, const void* data, const std::size_t bytes) const {
    {
        const profile_scope timer(profile(), profile_stage::upload);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, width, height, 1, format, type, data);
    }
    transfer(profile(), upload_kind::cached, bytes);
}

void gl_storage::depth_stencil(const unsigned id, const int width, const int height) const {
    const profile_scope timer(profile(), profile_stage::upload);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    if (ledger) { ledger->renderbuffers.replace(id, texture_storage_bytes(width, height, 4, false)); }
}

void gl_storage::delete_buffer(unsigned& id) const {
    if (id) {
        glDeleteBuffers(1, &id);
        if (ledger) { ledger->buffers.erase(id); }
        id = 0;
    }
}

void gl_storage::delete_texture(unsigned& id) const {
    if (id) {
        glDeleteTextures(1, &id);
        if (ledger) { ledger->textures.erase(id); }
        id = 0;
    }
}

void gl_storage::delete_renderbuffer(unsigned& id) const {
    if (id) {
        glDeleteRenderbuffers(1, &id);
        if (ledger) { ledger->renderbuffers.erase(id); }
        id = 0;
    }
}
