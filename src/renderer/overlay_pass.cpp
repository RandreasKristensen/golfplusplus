#include "renderer/overlay_pass.h"

#include <cstddef>

#include "renderer/gl_loader.h"

namespace {
// Enough for a busy HUD with text; the course map's marks grow it once on first open.
constexpr std::size_t initial_vertex_capacity = 8192;

void describe_overlay_vertex_layout() {
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
}
}

bool overlay_pass::init_stream(vertex_stream& stream, const std::size_t initial_capacity_bytes) {
    glGenVertexArrays(1, &stream.vao);
    if (stream.vao == 0) {
        return false;
    }

    glBindVertexArray(stream.vao);
    // init() leaves the buffer bound, which is what the attribute setup binds.
    if (!stream.buffer.init(initial_capacity_bytes)) {
        glBindVertexArray(0);
        return false;
    }
    describe_overlay_vertex_layout();
    glBindVertexArray(0);
    return true;
}

void overlay_pass::shutdown_stream(vertex_stream& stream) {
    stream.buffer.shutdown();
    if (stream.vao != 0) {
        glDeleteVertexArrays(1, &stream.vao);
        stream.vao = 0;
    }
}

bool overlay_pass::init(const std::string& vertex_path, const std::string& fragment_path) {
    shutdown();

    if (!shader_.load_from_files(vertex_path, fragment_path)) {
        return false;
    }

    batch_.vertices.reserve(initial_vertex_capacity);

    return init_stream(stream_, initial_vertex_capacity * sizeof(overlay_vertex));
}

void overlay_pass::shutdown() {
    shader_.shutdown();
    shutdown_stream(stream_);
    profile_ = nullptr;
    clear_overlay_batch(batch_);
}

overlay_batch& overlay_pass::begin(frame_profile* profile, const overlay_grid grid) {
    profile_ = profile;
    shader_.set_profile(profile);
    clear_overlay_batch(batch_);
    batch_.grid = grid;
    return batch_;
}

void overlay_pass::flush() {
    if (batch_.vertices.empty() || stream_.vao == 0) {
        clear_overlay_batch(batch_);
        return;
    }

    const std::size_t vertex_count = batch_.vertices.size();

    shader_.use();
    glBindVertexArray(stream_.vao);
    stream_.buffer.upload_changed(batch_.vertices.data(), vertex_count * sizeof(overlay_vertex), profile_);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertex_count));
    record_draw_call(profile_);

    glBindVertexArray(0);
    clear_overlay_batch(batch_);
}
