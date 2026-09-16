#pragma once

#include <glm/mat4x4.hpp>

#include "profiling/profiling.h"
#include "renderer/dynamic_buffer.h"
#include "renderer/shader.h"
#include "renderer/world_marker_batch.h"

// Uploads a world_marker_batch into one dynamic vertex buffer and draws it
// with one draw call per run and a single uniform set per frame.
//
// The GPU buffer only ever grows (see renderer/dynamic_buffer.h): a frame that
// fits the current capacity is streamed with glBufferSubData; a larger frame
// reallocates (orphans) the storage once before streaming.
struct world_marker_renderer {
    bool init(const char* vertex_path, const char* fragment_path);
    void shutdown();

    // Draws (and uploads) nothing for an empty batch. Leaves depth writes
    // enabled and no VAO bound on return.
    void draw(const world_marker_batch& batch, const glm::mat4& view_proj, frame_profile* profile);

private:
    shader_program shader_;
    unsigned int vao_ = 0;
    dynamic_vertex_buffer buffer_;
};
