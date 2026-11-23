#pragma once

// Grow-only streaming vertex buffer shared by every per-frame GL upload
// (overlay batch, world markers, flight path, the course map's sheet quad).
//
// A frame whose data fits the current GPU capacity is streamed with
// glBufferSubData into the existing storage. Only a frame that needs more
// room re-specifies (orphans) the storage with glBufferData(nullptr), sized by
// grow_buffer_capacity so a growing stream settles after a few frames.
// Capacity never shrinks. A stream whose bytes are the ones the buffer already
// holds (a parked crowd, a HUD that did not change) is not sent again, and one
// made of parts (the world markers' triangles) sends only the parts that changed
// (upload_changed).
//
// The sizing/decision rules below are GL-free and unit tested; this header
// pulls in no GL headers so tests can include it without a context.

#include "renderer/gl_storage.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "profiling/profiling.h"

// A 64-bit hash of `bytes` bytes, to tell a stream that did not change from one
// that did. Four independent lanes, so the multiplies overlap: a frame's
// vertices can be megabytes. A collision would leave one frame's data
// unsent, at odds of about one in 2^64 per comparison.
inline std::uint64_t hash_stream(const void* data, const std::size_t bytes) {
    const unsigned char* at = static_cast<const unsigned char*>(data);
    constexpr std::uint64_t multiplier = 0xFF51AFD7ED558CCDULL;
    std::uint64_t lane[4] = {0x9E3779B97F4A7C15ULL ^ bytes, 0xC2B2AE3D27D4EB4FULL, 0x165667B19E3779F9ULL, 0x27D4EB2F165667C5ULL};
    std::size_t i = 0;
    for (; i + 32 <= bytes; i += 32) {
        std::uint64_t word[4];
        std::memcpy(word, at + i, sizeof(word));
        for (int k = 0; k < 4; ++k) {
            lane[k] = (lane[k] ^ word[k]) * multiplier;
            lane[k] ^= lane[k] >> 32;
        }
    }
    std::uint64_t hash = lane[0] ^ (lane[1] * 3) ^ (lane[2] * 5) ^ (lane[3] * 7);
    for (; i + 8 <= bytes; i += 8) {
        std::uint64_t word = 0;
        std::memcpy(&word, at + i, sizeof(word));
        hash = (hash ^ word) * multiplier;
        hash ^= hash >> 32;
    }
    std::uint64_t tail = 0;
    for (std::size_t b = 0; i + b < bytes; ++b) { tail |= static_cast<std::uint64_t>(at[i + b]) << (8 * b); }
    hash = (hash ^ tail) * 0xC4CEB9FE1A85EC53ULL;
    return hash ^ (hash >> 29);
}

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
    explicit dynamic_vertex_buffer(gl_storage storage = {})
        : storage_(storage) {}

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
    void upload(const void* data, std::size_t bytes, frame_profile* profile, upload_kind kind = upload_kind::dynamic);

    // Like upload(), but sends only what differs from the last upload_changed's
    // when nothing else has been uploaded since. `data` is `part_count` consecutive
    // parts ending at byte offsets `part_ends` (the last is `bytes`); parts that
    // hash as they did are not sent, and a different layout sends everything.
    // Returns the bytes sent. Binds the buffer only when it sends.
    std::size_t upload_changed(const void* data, std::size_t bytes, const std::size_t* part_ends, std::size_t part_count,
                               frame_profile* profile);
    // One part.
    std::size_t upload_changed(const void* data, std::size_t bytes, frame_profile* profile) {
        return upload_changed(data, bytes, &bytes, 1, profile);
    }

    unsigned int id() const { return vbo_; }
    std::size_t capacity_bytes() const { return capacity_bytes_; }

private:
    gl_storage storage_;
    unsigned int vbo_ = 0;
    std::size_t capacity_bytes_ = 0;
    std::vector<std::size_t> held_ends_;
    std::vector<std::uint64_t> held_hashes_;
    std::vector<std::uint64_t> scratch_hashes_;
    bool held_known_ = false;
};
