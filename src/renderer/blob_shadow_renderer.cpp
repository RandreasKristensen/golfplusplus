#include "renderer/blob_shadow_renderer.h"

#include "renderer/dynamic_buffer.h"
#include "renderer/gl_loader.h"

#include <type_traits>

namespace {
constexpr GLuint corner_location = 0;
constexpr GLuint base_location = 1;
constexpr GLuint normal_location = 2;
constexpr GLuint shape_location = 3;

// Further forward than the drawn zone shapes (terrain_renderer.cpp), so a
// blob on a green or a bunker lies over it.
constexpr float blob_offset_factor = -2.0f;
constexpr float blob_offset_units = -4.0f;

// Covers a course's trees and a full room's players without growing on early frames.
constexpr std::size_t initial_instance_capacity = 4096;

constexpr float quad_corners[] = {-1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f};
constexpr GLsizei quad_corner_count = 6;

void instance_attribute(const GLuint location, const std::size_t offset) {
    glEnableVertexAttribArray(location);
    glVertexAttribPointer(location, 3, GL_FLOAT, GL_FALSE, sizeof(render_blob_shadow), reinterpret_cast<void*>(offset));
    glVertexAttribDivisor(location, 1);
}
}

static_assert(sizeof(render_blob_shadow) == 9 * sizeof(float), "render_blob_shadow must be nine packed floats");
static_assert(offsetof(render_blob_shadow, normal) == 3 * sizeof(float), "render_blob_shadow::normal must follow base");
static_assert(offsetof(render_blob_shadow, stretch) == 6 * sizeof(float), "render_blob_shadow's shape must follow normal");
static_assert(std::is_standard_layout<render_blob_shadow>::value, "render_blob_shadow must be standard layout");

bool blob_shadow_renderer::init(const std::string& vertex_path, const std::string& fragment_path) {
    shutdown();
    if (!shader_.load_from_files(vertex_path, fragment_path)) {
        return false;
    }
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &corner_vbo_);
    glGenBuffers(1, &instance_vbo_);
    if (vao_ == 0 || corner_vbo_ == 0 || instance_vbo_ == 0) {
        shutdown();
        return false;
    }
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, corner_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_corners), quad_corners, GL_STATIC_DRAW);
    glEnableVertexAttribArray(corner_location);
    glVertexAttribPointer(corner_location, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    capacity_ = initial_instance_capacity;
    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity_ * sizeof(render_blob_shadow)), nullptr, GL_DYNAMIC_DRAW);
    instance_attribute(base_location, offsetof(render_blob_shadow, base));
    instance_attribute(normal_location, offsetof(render_blob_shadow, normal));
    instance_attribute(shape_location, offsetof(render_blob_shadow, stretch));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void blob_shadow_renderer::shutdown() {
    for (unsigned int* buffer : {&corner_vbo_, &instance_vbo_}) {
        if (*buffer != 0) {
            glDeleteBuffers(1, buffer);
            *buffer = 0;
        }
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    shader_.shutdown();
    capacity_ = 0;
    tree_count_ = 0;
    trees_uploaded_ = false;
}

void blob_shadow_renderer::draw(const std::vector<render_blob_shadow>* trees, const std::uint64_t trees_revision,
                                const std::vector<render_blob_shadow>& frame, const glm::mat4& view_proj,
                                frame_profile* profile) {
    if (shader_.id() == 0) {
        return;
    }
    const std::size_t tree_count = trees != nullptr ? trees->size() : 0;
    const bool trees_changed = !trees_uploaded_ || trees_revision_ != trees_revision || tree_count_ != tree_count;
    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
    const std::size_t required = tree_count + frame.size();
    if (required > capacity_) {
        capacity_ = grow_buffer_capacity(capacity_, required);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity_ * sizeof(render_blob_shadow)), nullptr, GL_DYNAMIC_DRAW);
        record_buffer_upload(profile, 0);
        trees_uploaded_ = false;
    }
    if (trees_changed || !trees_uploaded_) {
        if (tree_count > 0) {
            const std::size_t bytes = tree_count * sizeof(render_blob_shadow);
            glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), trees->data());
            record_buffer_write(profile, bytes);
        }
        tree_count_ = tree_count;
        trees_revision_ = trees_revision;
        trees_uploaded_ = true;
    }
    if (!frame.empty()) {
        const std::size_t bytes = frame.size() * sizeof(render_blob_shadow);
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(tree_count * sizeof(render_blob_shadow)),
                        static_cast<GLsizeiptr>(bytes), frame.data());
        record_buffer_write(profile, bytes);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (required == 0) {
        return;
    }

    shader_.set_profile(profile);
    shader_.use();
    shader_.set_mat4("u_view_proj", view_proj);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(blob_offset_factor, blob_offset_units);
    glBindVertexArray(vao_);
    glDrawArraysInstanced(GL_TRIANGLES, 0, quad_corner_count, static_cast<GLsizei>(required));
    record_draw_call(profile);
    glBindVertexArray(0);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}
