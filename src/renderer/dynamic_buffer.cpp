#include "renderer/dynamic_buffer.h"

#include "renderer/gl_loader.h"

#include <algorithm>

bool dynamic_vertex_buffer::init(const std::size_t initial_capacity_bytes) {
    shutdown();

    glGenBuffers(1, &vbo_);
    if (vbo_ == 0) {
        return false;
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    capacity_bytes_ = initial_capacity_bytes;
    storage_.buffer(vbo_, GL_ARRAY_BUFFER, capacity_bytes_, nullptr, GL_DYNAMIC_DRAW);
    return true;
}

void dynamic_vertex_buffer::shutdown() {
    storage_.delete_buffer(vbo_);
    capacity_bytes_ = 0;
    held_known_ = false;
}

void dynamic_vertex_buffer::upload(const void* data, const std::size_t bytes, frame_profile* profile, const upload_kind kind) {
    if (vbo_ == 0 || data == nullptr) {
        return;
    }
    held_known_ = false;

    const buffer_upload_plan plan = plan_buffer_upload(capacity_bytes_, bytes);
    if (plan.action == buffer_upload_action::none) {
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    if (plan.action == buffer_upload_action::reallocate) {
        storage_.buffer(vbo_, GL_ARRAY_BUFFER, plan.capacity_bytes, nullptr, GL_DYNAMIC_DRAW, profile);
        capacity_bytes_ = plan.capacity_bytes;
    }
    storage_.write(GL_ARRAY_BUFFER, 0, bytes, data, kind, profile);
}

std::size_t dynamic_vertex_buffer::upload_changed(const void* data, const std::size_t bytes, const std::size_t* part_ends,
                                                  const std::size_t part_count, frame_profile* profile) {
    if (vbo_ == 0 || data == nullptr || bytes == 0 || part_count == 0) {
        return 0;
    }
    const unsigned char* at = static_cast<const unsigned char*>(data);
    std::vector<std::uint64_t>& hashes = scratch_hashes_;
    hashes.resize(part_count);
    std::size_t begin = 0;
    for (std::size_t p = 0; p < part_count; ++p) {
        hashes[p] = hash_stream(at + begin, part_ends[p] - begin);
        begin = part_ends[p];
    }
    const bool same_layout = held_known_ && held_ends_.size() == part_count &&
                             std::equal(held_ends_.begin(), held_ends_.end(), part_ends);
    if (!same_layout) {
        upload(data, bytes, profile);
        held_ends_.assign(part_ends, part_ends + part_count);
        held_hashes_ = hashes;
        held_known_ = true;
        return bytes;
    }
    std::size_t sent = 0;
    begin = 0;
    for (std::size_t p = 0; p < part_count; ++p) {
        const std::size_t length = part_ends[p] - begin;
        if (length > 0 && hashes[p] != held_hashes_[p]) {
            if (sent == 0) {
                glBindBuffer(GL_ARRAY_BUFFER, vbo_);
            }
            storage_.write(GL_ARRAY_BUFFER, begin, length, at + begin, upload_kind::dynamic, profile);
            held_hashes_[p] = hashes[p];
            sent += length;
        }
        begin = part_ends[p];
    }
    return sent;
}
