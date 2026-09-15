#pragma once

// GL side of the batched overlay. Owns one dynamic VBO/VAO plus the tiny
// overlay shader, and turns an overlay_batch into a single glDrawArrays.
//
// Usage per frame (inside render_overlay, blend state already set):
//     overlay_batch& batch = overlay_pass_.begin(profile);
//     draw_overlay_quad(batch, ...);         // any number of primitives
//     overlay_pass_.flush();                 // one upload + one draw
//
// flush() must be called before anything that is not a batched overlay quad
// (other shaders, VAOs, blend/depth/scissor changes) so painter's order holds.

#include <cstddef>

#include "profiling/profiling.h"
#include "renderer/overlay_batch.h"
#include "renderer/shader.h"

struct overlay_pass {
    bool init(const char* vertex_path, const char* fragment_path);
    void shutdown();

    // Clears the batch and points profiling at `profile` (null = off).
    overlay_batch& begin(frame_profile* profile);
    overlay_batch& batch() { return batch_; }

    // Uploads and draws everything queued since the last flush, then clears
    // the batch (capacity is kept). No-op when empty.
    void flush();

private:
    void ensure_gpu_capacity(std::size_t byte_count);

    shader_program shader_;
    overlay_batch batch_;
    frame_profile* profile_ = nullptr;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    std::size_t gpu_capacity_bytes_ = 0;
    // glBufferSubData is not part of the SDL fallback loader table, so it is
    // resolved locally (like gl_timer does for query objects). When missing,
    // flush() falls back to re-specifying the buffer with glBufferData.
    void* buffer_sub_data_ = nullptr;
};
