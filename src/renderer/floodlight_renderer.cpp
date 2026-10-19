#include "renderer/floodlight_renderer.h"

#include <SDL.h>

#include <type_traits>

#include "renderer/gl_loader.h"

namespace {
constexpr GLuint position_location = 0;
constexpr GLuint normal_location = 1;
constexpr GLuint color_location = 2;
// shadow_map.vert's instance attributes, left off in this VAO so they read
// the constant values draw_casters sets.
constexpr GLuint instance_offset_location = 3;
constexpr GLuint instance_scale_location = 4;
constexpr GLuint lamp_location = 5;

void describe_attribute(const GLuint location, const GLint size, const std::size_t offset) {
    glEnableVertexAttribArray(location);
    glVertexAttribPointer(location, size, GL_FLOAT, GL_FALSE, sizeof(floodlight_vertex), reinterpret_cast<void*>(offset));
}
}

static_assert(std::is_standard_layout<floodlight_vertex>::value, "floodlight_vertex must be standard layout");
static_assert(sizeof(floodlight_vertex) == 10 * sizeof(float), "floodlight_vertex must be ten packed floats");

bool floodlight_renderer::init(const std::string& vertex_path, const std::string& fragment_path) {
    shutdown();
    if (!shader_.load_from_files(vertex_path, fragment_path)) {
        return false;
    }
    glGenVertexArrays(1, &vao_);
    if (vao_ == 0) {
        SDL_Log("Floodlight renderer failed to allocate GL objects.");
        shutdown();
        return false;
    }
    glBindVertexArray(vao_);
    // init() leaves the buffer bound for the attribute pointers below.
    if (!buffer_.init(0)) {
        SDL_Log("Floodlight renderer failed to allocate GL objects.");
        glBindVertexArray(0);
        shutdown();
        return false;
    }
    describe_attribute(position_location, 3, offsetof(floodlight_vertex, position));
    describe_attribute(normal_location, 3, offsetof(floodlight_vertex, normal));
    describe_attribute(color_location, 3, offsetof(floodlight_vertex, color));
    describe_attribute(lamp_location, 1, offsetof(floodlight_vertex, lamp));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void floodlight_renderer::shutdown() {
    buffer_.shutdown();
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    shader_.shutdown();
    uploaded_ = false;
}

bool floodlight_renderer::prepare(const render_floodlights* floodlights, frame_profile* profile) {
    if (floodlights == nullptr || floodlights->vertices.empty() || vao_ == 0) {
        return false;
    }
    if (!uploaded_ || uploaded_revision_ != floodlights->revision) {
        buffer_.upload(floodlights->vertices.data(), floodlights->vertices.size() * sizeof(floodlight_vertex), profile);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        vertex_count_ = floodlights->vertices.size();
        uploaded_revision_ = floodlights->revision;
        uploaded_ = true;
    }
    glBindVertexArray(vao_);
    return true;
}

void floodlight_renderer::draw(const render_floodlights* floodlights, const glm::mat4& view_proj, frame_profile* profile) {
    if (shader_.id() == 0 || !prepare(floodlights, profile)) {
        return;
    }
    shader_.set_profile(profile);
    shader_.use();
    shader_.set_mat4("u_view_proj", view_proj);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertex_count_));
    record_draw_call(profile);
    glBindVertexArray(0);
}

void floodlight_renderer::draw_casters(const render_floodlights* floodlights, const shader_program& depth_shader,
                                       const glm::mat4& view_proj, frame_profile* profile) {
    if (!prepare(floodlights, profile)) {
        return;
    }
    depth_shader.set_mat4("u_view_proj", view_proj);
    glVertexAttrib3f(instance_offset_location, 0.0f, 0.0f, 0.0f);
    glVertexAttrib3f(instance_scale_location, 1.0f, 1.0f, 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertex_count_));
    record_draw_call(profile);
    glBindVertexArray(0);
}
