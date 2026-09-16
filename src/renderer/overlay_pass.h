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
#include <cstdint>
#include <vector>

#include "profiling/profiling.h"
#include "renderer/dynamic_buffer.h"
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

    // Draws overlay geometry that is rebuilt rarely (the course map terrain
    // fill) from its own retained buffer: the vertices are uploaded only when
    // `revision` changes, not every frame. Flushes the streaming batch first
    // so the retained geometry lands in submission order.
    void draw_retained(const std::vector<overlay_vertex>& vertices, std::uint64_t revision);

private:
    // One VAO over one dynamic buffer; the overlay vertex layout for both.
    struct vertex_stream {
        unsigned int vao = 0;
        dynamic_vertex_buffer buffer;
    };

    bool init_stream(vertex_stream& stream, std::size_t initial_capacity_bytes);
    void shutdown_stream(vertex_stream& stream);

    shader_program shader_;
    overlay_batch batch_;
    frame_profile* profile_ = nullptr;
    vertex_stream stream_;
    vertex_stream retained_stream_;
    std::size_t retained_vertex_count_ = 0;
    std::uint64_t retained_revision_ = 0;
    bool retained_uploaded_ = false;
};
