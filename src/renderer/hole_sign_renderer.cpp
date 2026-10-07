#include "renderer/hole_sign_renderer.h"

#include <SDL.h>

#include <cstddef>
#include <type_traits>

#include "renderer/gl_loader.h"

namespace {
constexpr GLuint position_location = 0;
constexpr GLuint normal_location = 1;
constexpr GLuint color_location = 2;
constexpr GLuint uv_location = 3;
constexpr GLuint textured_location = 4;
constexpr int face_texture_unit = 0;

void describe_attribute(const GLuint location, const GLint size, const std::size_t offset) {
    glEnableVertexAttribArray(location);
    glVertexAttribPointer(location, size, GL_FLOAT, GL_FALSE, sizeof(hole_sign_vertex), reinterpret_cast<void*>(offset));
}
}

static_assert(std::is_standard_layout<hole_sign_vertex>::value, "hole_sign_vertex must be standard layout");
static_assert(sizeof(hole_sign_vertex) == 12 * sizeof(float), "hole_sign_vertex must be twelve packed floats");

bool hole_sign_renderer::init(const std::string& vertex_path, const std::string& fragment_path) {
    shutdown();
    if (!shader_.load_from_files(vertex_path, fragment_path)) {
        shutdown();
        return false;
    }
    glGenVertexArrays(1, &vao_);
    if (vao_ == 0) {
        SDL_Log("Hole sign renderer failed to allocate GL objects.");
        shutdown();
        return false;
    }
    glBindVertexArray(vao_);
    // init() leaves the buffer bound for the attribute pointers below.
    if (!buffer_.init(0)) {
        SDL_Log("Hole sign renderer failed to allocate GL objects.");
        glBindVertexArray(0);
        shutdown();
        return false;
    }
    describe_attribute(position_location, 3, offsetof(hole_sign_vertex, position));
    describe_attribute(normal_location, 3, offsetof(hole_sign_vertex, normal));
    describe_attribute(color_location, 3, offsetof(hole_sign_vertex, color));
    describe_attribute(uv_location, 2, offsetof(hole_sign_vertex, uv));
    describe_attribute(textured_location, 1, offsetof(hole_sign_vertex, textured));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    shader_.use();
    shader_.set_int("u_face", face_texture_unit);
    return true;
}

void hole_sign_renderer::shutdown() {
    for (texture& face : faces_) {
        face.shutdown();
    }
    faces_.clear();
    buffer_.shutdown();
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    shader_.shutdown();
    uploaded_ = false;
}

void hole_sign_renderer::upload(const render_hole_signs& signs, frame_profile* profile) {
    buffer_.upload(signs.vertices.data(), signs.vertices.size() * sizeof(hole_sign_vertex), profile);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    for (texture& face : faces_) {
        face.shutdown();
    }
    faces_.assign(signs.signs.size(), texture{});
    for (std::size_t i = 0; i < signs.signs.size(); ++i) {
        // Chunky texels up close and mipmaps far off. The face's wood frame
        // is one colour all round, so the repeat at its edges never shows.
        faces_[i].upload(signs.signs[i].face, texture_sampling::tiled_detail);
    }
    uploaded_revision_ = signs.revision;
    uploaded_ = true;
}

void hole_sign_renderer::draw(const render_hole_signs* signs,
                              const glm::mat4& view_proj,
                              const glm::vec3& light_direction,
                              const scene_haze& haze,
                              frame_profile* profile) {
    if (signs == nullptr || signs->signs.empty() || shader_.id() == 0 || vao_ == 0) {
        return;
    }
    if (!uploaded_ || uploaded_revision_ != signs->revision) {
        upload(*signs, profile);
    }

    shader_.set_profile(profile);
    shader_.use();
    shader_.set_mat4("u_view_proj", view_proj);
    shader_.set_vec3("u_light_dir", light_direction);
    set_scene_haze_uniforms(shader_, haze);
    glBindVertexArray(vao_);
    for (std::size_t i = 0; i < signs->signs.size() && i < faces_.size(); ++i) {
        const render_hole_sign& sign = signs->signs[i];
        faces_[i].bind(face_texture_unit);
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(sign.first_vertex), static_cast<GLsizei>(sign.vertex_count));
        record_draw_call(profile);
    }
    glBindVertexArray(0);
}
