#pragma once

// Grow-only streaming vertex buffer shared by every per-frame GL upload
// (overlay batch, world markers, flight path, retained course-map fill).
//
// A frame whose data fits the current GPU capacity is streamed with
// glBufferSubData into the existing storage. Only a frame that needs more
// room re-specifies (orphans) the storage with glBufferData(nullptr), sized by
// grow_buffer_capacity so a growing stream settles after a few frames.
// Capacity never shrinks.
//
// The sizing/decision rules below are GL-free and unit tested; this header
// pulls in no GL headers so tests can include it without a context.

#include <cstddef>

#include "profiling/profiling.h"

// Returns `current` when `required` fits, otherwise at least double `current`
// (and at least `required`).
inline std::size_t grow_buffer_capacity(const std::size_t current, const std::size_t required) {
    if (required <= current) {
        return current;
    }
    const std::size_t doubled = current * 2U;
    return required > doubled ? required : doubled;
}

enum class buffer_upload_action {
    none,        // nothing to upload
    stream,      // glBufferSubData into the existing storage
    reallocate   // glBufferData(nullptr) to `capacity_bytes`, then glBufferSubData
};

struct buffer_upload_plan {
    buffer_upload_action action = buffer_upload_action::none;
    std::size_t capacity_bytes = 0;  // storage size after the upload
};

inline buffer_upload_plan plan_buffer_upload(const std::size_t capacity_bytes, const std::size_t required_bytes) {
    buffer_upload_plan plan;
    plan.capacity_bytes = capacity_bytes;
    if (required_bytes == 0) {
        return plan;
    }
    if (required_bytes <= capacity_bytes) {
        plan.action = buffer_upload_action::stream;
        return plan;
    }
    plan.action = buffer_upload_action::reallocate;
    plan.capacity_bytes = grow_buffer_capacity(capacity_bytes, required_bytes);
    return plan;
}

// GL side. Owns one GL_ARRAY_BUFFER object. Not copyable: it owns GL names.
class dynamic_vertex_buffer {
public:
    dynamic_vertex_buffer() = default;
    dynamic_vertex_buffer(const dynamic_vertex_buffer&) = delete;
    dynamic_vertex_buffer& operator=(const dynamic_vertex_buffer&) = delete;

    // Creates the buffer and reserves `initial_capacity_bytes` (may be 0).
    // Leaves the buffer bound to GL_ARRAY_BUFFER so the caller can describe
    // vertex attributes into its VAO. Returns false if no buffer was created.
    bool init(std::size_t initial_capacity_bytes);
    void shutdown();

    // Binds the buffer to GL_ARRAY_BUFFER (left bound) and uploads `bytes`
    // from `data` following plan_buffer_upload. Zero bytes uploads nothing
    // and does not bind. Counts the GL work into `profile` (null = off).
    void upload(const void* data, std::size_t bytes, frame_profile* profile);

    unsigned int id() const { return vbo_; }
    std::size_t capacity_bytes() const { return capacity_bytes_; }

private:
    unsigned int vbo_ = 0;
    std::size_t capacity_bytes_ = 0;
};
