#include "renderer/world_marker_renderer.h"

#include <SDL.h>

#include <cstddef>
#include <type_traits>

#include "core/gl_loader.h"
#include "renderer/gl_proc.h"

namespace {
// glBufferSubData is not part of the base loader (see renderer/gl_proc.h).
using buffer_sub_data_fn = void (APIENTRY*)(GLenum, GLintptr, GLsizeiptr, const void*);

constexpr GLuint position_location = 0;
constexpr GLuint color_location = 1;

// Covers single-hole aiming (~1.7k vertices: tee, cup, flagstick, 28 aim
// dots, club) and a six-hole hub (~1.1k) without growing on early frames.
constexpr std::size_t initial_vertex_capacity = 2048;
}

// The vertex buffer is uploaded as raw floats: position.xyz then color.rgba.
static_assert(sizeof(world_marker_vertex) == 7 * sizeof(float), "world_marker_vertex must be seven packed floats");
static_assert(offsetof(world_marker_vertex, color) == 3 * sizeof(float), "world_marker_vertex::color must follow position");
static_assert(std::is_standard_layout<world_marker_vertex>::value, "world_marker_vertex must be standard layout");

bool world_marker_renderer::init(const char* vertex_path, const char* fragment_path) {
    shutdown();

    buffer_sub_data_ = load_gl_proc("glBufferSubData");
    if (buffer_sub_data_ == nullptr) {
        SDL_Log("glBufferSubData unavailable; world marker rendering requires OpenGL 3.3.");
        shutdown();
        return false;
    }

    if (!shader_.load_from_files(vertex_path, fragment_path)) {
        shutdown();
        return false;
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    if (vao_ == 0 || vbo_ == 0) {
        SDL_Log("World marker renderer failed to allocate GL objects.");
        shutdown();
        return false;
    }

    capacity_bytes_ = initial_vertex_capacity * sizeof(world_marker_vertex);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity_bytes_), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(position_location);
    glVertexAttribPointer(position_location,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(world_marker_vertex),
                          reinterpret_cast<void*>(offsetof(world_marker_vertex, position)));
    glEnableVertexAttribArray(color_location);
    glVertexAttribPointer(color_location,
                          4,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(world_marker_vertex),
                          reinterpret_cast<void*>(offsetof(world_marker_vertex, color)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void world_marker_renderer::shutdown() {
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }

    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }

    shader_.shutdown();
    capacity_bytes_ = 0;
    buffer_sub_data_ = nullptr;
}

void world_marker_renderer::upload(const world_marker_batch& batch, frame_profile* profile) {
    const std::size_t bytes = batch.vertices().size() * sizeof(world_marker_vertex);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const std::size_t capacity = grow_buffer_capacity(capacity_bytes_, bytes);
    if (capacity != capacity_bytes_) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity), nullptr, GL_DYNAMIC_DRAW);
        capacity_bytes_ = capacity;
    }
    reinterpret_cast<buffer_sub_data_fn>(buffer_sub_data_)(GL_ARRAY_BUFFER,
                                                           0,
                                                           static_cast<GLsizeiptr>(bytes),
                                                           batch.vertices().data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    record_buffer_upload(profile, bytes);
}

void world_marker_renderer::draw(const world_marker_batch& batch,
                                 const glm::mat4& view_proj,
                                 frame_profile* profile) {
    if (batch.empty() || shader_.id() == 0 || vao_ == 0 || buffer_sub_data_ == nullptr) {
        return;
    }

    upload(batch, profile);

    shader_.set_profile(profile);
    shader_.use();
    shader_.set_mat4("u_view_proj", view_proj);

    glBindVertexArray(vao_);
    for (const world_marker_run& run : batch.runs()) {
        if (run.count == 0) {
            continue;
        }
        glDepthMask(run.depth_write ? GL_TRUE : GL_FALSE);
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(run.first), static_cast<GLsizei>(run.count));
        record_draw_call(profile);
    }
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
}
