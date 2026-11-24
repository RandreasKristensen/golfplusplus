#include "renderer/world_bolt_renderer.h"

#include "renderer/gl_loader.h"

#include <cstddef>

namespace {
constexpr GLuint position_location = 0;
constexpr GLuint ribbon_location = 1;
}

bool world_bolt_renderer::init(const std::string& vertex_path, const std::string& fragment_path) {
    shutdown();
    if (!shader_.load_from_files(vertex_path, fragment_path)) {
        return false;
    }
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    // init() leaves the buffer bound for the attribute pointers below.
    if (vao_ == 0 || !buffer_.init(0)) {
        glBindVertexArray(0);
        shutdown();
        return false;
    }
    glEnableVertexAttribArray(position_location);
    glVertexAttribPointer(position_location, 3, GL_FLOAT, GL_FALSE, sizeof(world_bolt_vertex),
                          reinterpret_cast<void*>(offsetof(world_bolt_vertex, position)));
    glEnableVertexAttribArray(ribbon_location);
    glVertexAttribPointer(ribbon_location, 2, GL_FLOAT, GL_FALSE, sizeof(world_bolt_vertex),
                          reinterpret_cast<void*>(offsetof(world_bolt_vertex, across)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void world_bolt_renderer::shutdown() {
    buffer_.shutdown();
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    shader_.shutdown();
}

void world_bolt_renderer::draw(const render_lightning& lightning, const glm::vec3& camera, const glm::mat4& view_proj,
                               frame_profile* profile) {
    vertices_ = make_world_bolt(lightning, camera);
    if (shader_.id() == 0 || vertices_.empty()) {
        return;
    }
    buffer_.upload(vertices_.data(), vertices_.size() * sizeof(world_bolt_vertex), profile);
    shader_.set_profile(profile);
    shader_.use();
    shader_.set_mat4("u_view_proj", view_proj);
    shader_.set_vec3("u_color", lightning.color * (lightning.bolt_brightness * lightning.flash));
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
    record_draw_call(profile);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}
