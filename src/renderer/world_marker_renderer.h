#pragma once

#include <cstddef>

#include <glm/mat4x4.hpp>

#include "profiling/profiling.h"
#include "renderer/shader.h"
#include "renderer/world_marker_batch.h"

// Uploads a world_marker_batch into one dynamic vertex buffer and draws it
// with one draw call per run and a single uniform set per frame.
//
// The GPU buffer only ever grows: a frame that fits the current capacity is
// streamed with glBufferSubData; a larger frame reallocates (orphans) the
// storage once, to at least double the capacity, before streaming.
struct world_marker_renderer {
    bool init(const char* vertex_path, const char* fragment_path);
    void shutdown();

    // Draws (and uploads) nothing for an empty batch. Leaves depth writes
    // enabled and no VAO bound on return.
    void draw(const world_marker_batch& batch, const glm::mat4& view_proj, frame_profile* profile);

private:
    void upload(const world_marker_batch& batch, frame_profile* profile);

    shader_program shader_;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    std::size_t capacity_bytes_ = 0;

    // Loaded through SDL_GL_GetProcAddress into a member, never into a global.
    void* buffer_sub_data_ = nullptr;
};
