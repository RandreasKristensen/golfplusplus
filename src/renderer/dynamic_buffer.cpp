#include "renderer/dynamic_buffer.h"

#include "renderer/gl_loader.h"

bool dynamic_vertex_buffer::init(const std::size_t initial_capacity_bytes) {
    shutdown();

    glGenBuffers(1, &vbo_);
    if (vbo_ == 0) {
        return false;
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    capacity_bytes_ = initial_capacity_bytes;
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity_bytes_), nullptr, GL_DYNAMIC_DRAW);
    return true;
}

void dynamic_vertex_buffer::shutdown() {
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    capacity_bytes_ = 0;
}

void dynamic_vertex_buffer::upload(const void* data, const std::size_t bytes, frame_profile* profile) {
    if (vbo_ == 0 || data == nullptr) {
        return;
    }

    const buffer_upload_plan plan = plan_buffer_upload(capacity_bytes_, bytes);
    if (plan.action == buffer_upload_action::none) {
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    if (plan.action == buffer_upload_action::reallocate) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(plan.capacity_bytes), nullptr, GL_DYNAMIC_DRAW);
        capacity_bytes_ = plan.capacity_bytes;
        record_buffer_upload(profile, 0U);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), data);
    record_buffer_write(profile, bytes);
}
