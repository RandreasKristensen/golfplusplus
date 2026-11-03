#include "renderer/prop_renderer.h"

#include "renderer/gl_loader.h"

#include <SDL.h>

#include <cstddef>
#include <type_traits>

namespace {
constexpr GLuint position_location = 0;
constexpr GLuint normal_location = 1;
constexpr GLuint color_location = 2;
constexpr GLuint alpha_location = 3;
constexpr GLuint glow_location = 4;

// A room's carts and what is held, without growing on early frames.
constexpr std::size_t initial_vertex_capacity = 16384;

void attribute(const GLuint location, const GLint size, const std::size_t offset) {
    glEnableVertexAttribArray(location);
    glVertexAttribPointer(location, size, GL_FLOAT, GL_FALSE, sizeof(prop_vertex), reinterpret_cast<void*>(offset));
}
}

// Uploaded as raw floats: position.xyz, normal.xyz, color.rgb, alpha, glow.
static_assert(sizeof(prop_vertex) == 11 * sizeof(float), "prop_vertex must be eleven packed floats");
static_assert(std::is_standard_layout<prop_vertex>::value, "prop_vertex must be standard layout");

bool prop_renderer::init(const std::string& vertex_path, const std::string& fragment_path) {
    shutdown();
    if (!shader_.load_from_files(vertex_path, fragment_path)) {
        return false;
    }
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    // init() leaves the buffer bound for the attribute pointers below.
    if (vao_ == 0 || !buffer_.init(initial_vertex_capacity * sizeof(prop_vertex))) {
        SDL_Log("The prop renderer failed to allocate GL objects.");
        glBindVertexArray(0);
        shutdown();
        return false;
    }
    attribute(position_location, 3, offsetof(prop_vertex, position));
    attribute(normal_location, 3, offsetof(prop_vertex, normal));
    attribute(color_location, 3, offsetof(prop_vertex, color));
    attribute(alpha_location, 1, offsetof(prop_vertex, alpha));
    attribute(glow_location, 1, offsetof(prop_vertex, glow));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void prop_renderer::shutdown() {
    buffer_.shutdown();
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    shader_.shutdown();
}

void prop_renderer::draw(const prop_batch& batch, const glm::mat4& view_proj, frame_profile* profile) {
    if (batch.empty() || shader_.id() == 0 || vao_ == 0) {
        return;
    }
    shader_.set_profile(profile);
    shader_.use();
    shader_.set_mat4("u_view_proj", view_proj);
    glBindVertexArray(vao_);
    draw_vertices(batch.opaque(), profile);
    if (!batch.blended().empty()) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        draw_vertices(batch.blended(), profile);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
    glBindVertexArray(0);
}

void prop_renderer::draw_vertices(const std::vector<prop_vertex>& vertices, frame_profile* profile) {
    if (vertices.empty()) {
        return;
    }
    buffer_.upload(vertices.data(), vertices.size() * sizeof(prop_vertex), profile);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    record_draw_call(profile);
}
