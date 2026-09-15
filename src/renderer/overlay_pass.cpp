#include "renderer/overlay_pass.h"

#include <SDL.h>

#include <algorithm>
#include <cstddef>

#include "core/gl_loader.h"

namespace {
// Enough for a busy HUD with text; the course map grows it once on first open.
constexpr std::size_t initial_vertex_capacity = 8192;

#if !defined(VCR_GOLF_USE_GLAD)
using buffer_sub_data_fn = void (APIENTRY*)(GLenum, GLintptr, GLsizeiptr, const void*);
#endif
}

bool overlay_pass::init(const char* vertex_path, const char* fragment_path) {
    shutdown();

    if (!shader_.load_from_files(vertex_path, fragment_path)) {
        return false;
    }

#if defined(VCR_GOLF_USE_GLAD)
    buffer_sub_data_ = nullptr;
#else
    buffer_sub_data_ = reinterpret_cast<void*>(SDL_GL_GetProcAddress("glBufferSubData"));
    if (buffer_sub_data_ == nullptr) {
        SDL_Log("glBufferSubData unavailable; overlay batch re-specifies its buffer each flush.");
    }
#endif

    batch_.vertices.reserve(initial_vertex_capacity);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    gpu_capacity_bytes_ = initial_vertex_capacity * sizeof(overlay_vertex);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(gpu_capacity_bytes_), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,
                          2,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(overlay_vertex),
                          reinterpret_cast<void*>(offsetof(overlay_vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,
                          4,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(overlay_vertex),
                          reinterpret_cast<void*>(offsetof(overlay_vertex, color)));
    glBindVertexArray(0);
    return true;
}

void overlay_pass::shutdown() {
    shader_.shutdown();
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    gpu_capacity_bytes_ = 0;
    buffer_sub_data_ = nullptr;
    profile_ = nullptr;
    clear_overlay_batch(batch_);
}

overlay_batch& overlay_pass::begin(frame_profile* profile) {
    profile_ = profile;
    shader_.set_profile(profile);
    clear_overlay_batch(batch_);
    return batch_;
}

void overlay_pass::ensure_gpu_capacity(const std::size_t byte_count) {
    if (byte_count <= gpu_capacity_bytes_) {
        return;
    }

    // Geometric growth so a large overlay (course map) settles after one or
    // two frames instead of reallocating GPU storage every frame.
    std::size_t next = std::max<std::size_t>(gpu_capacity_bytes_, sizeof(overlay_vertex));
    while (next < byte_count) {
        next *= 2;
    }
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(next), nullptr, GL_DYNAMIC_DRAW);
    gpu_capacity_bytes_ = next;
}

void overlay_pass::flush() {
    if (batch_.vertices.empty() || vao_ == 0 || vbo_ == 0) {
        clear_overlay_batch(batch_);
        return;
    }

    const std::size_t vertex_count = batch_.vertices.size();
    const std::size_t byte_count = vertex_count * sizeof(overlay_vertex);

    shader_.use();
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

#if defined(VCR_GOLF_USE_GLAD)
    ensure_gpu_capacity(byte_count);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(byte_count), batch_.vertices.data());
#else
    if (buffer_sub_data_ != nullptr) {
        ensure_gpu_capacity(byte_count);
        reinterpret_cast<buffer_sub_data_fn>(buffer_sub_data_)(GL_ARRAY_BUFFER,
                                                               0,
                                                               static_cast<GLsizeiptr>(byte_count),
                                                               batch_.vertices.data());
    } else {
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(byte_count),
                     batch_.vertices.data(),
                     GL_DYNAMIC_DRAW);
        gpu_capacity_bytes_ = byte_count;
    }
#endif
    record_buffer_upload(profile_, byte_count);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertex_count));
    record_draw_call(profile_);

    glBindVertexArray(0);
    clear_overlay_batch(batch_);
}
