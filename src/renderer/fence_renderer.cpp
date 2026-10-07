#include "renderer/fence_renderer.h"

#include <SDL.h>

#include <optional>
#include <type_traits>
#include <vector>

#include "renderer/bmp_image.h"
#include "renderer/gl_loader.h"

namespace {
constexpr GLuint position_location = 0;
constexpr GLuint normal_location = 1;
constexpr GLuint color_location = 2;
constexpr GLuint uv_location = 3;
constexpr int net_texture_unit = 0;

void describe_attribute(const GLuint location, const GLint size, const std::size_t offset) {
    glEnableVertexAttribArray(location);
    glVertexAttribPointer(location, size, GL_FLOAT, GL_FALSE, sizeof(fence_vertex), reinterpret_cast<void*>(offset));
}
}

static_assert(std::is_standard_layout<fence_vertex>::value, "fence_vertex must be standard layout");
static_assert(sizeof(fence_vertex) == 11 * sizeof(float), "fence_vertex must be eleven packed floats");

bool fence_renderer::init(const std::string& vertex_path, const std::string& fragment_path, const std::string& net_image) {
    shutdown();
    const std::optional<rgba_image> net = load_bmp_file(net_image);
    if (!net) {
        SDL_Log("%s is missing or not an uncompressed 24/32-bit BMP", net_image.c_str());
        return false;
    }
    if (!shader_.load_from_files(vertex_path, fragment_path) || !net_.upload(*net, texture_sampling::tiled_detail)) {
        shutdown();
        return false;
    }
    glGenVertexArrays(1, &vao_);
    if (vao_ == 0) {
        SDL_Log("Fence renderer failed to allocate GL objects.");
        shutdown();
        return false;
    }
    glBindVertexArray(vao_);
    // init() leaves the buffer bound for the attribute pointers below.
    if (!buffer_.init(0)) {
        SDL_Log("Fence renderer failed to allocate GL objects.");
        glBindVertexArray(0);
        shutdown();
        return false;
    }
    describe_attribute(position_location, 3, offsetof(fence_vertex, position));
    describe_attribute(normal_location, 3, offsetof(fence_vertex, normal));
    describe_attribute(color_location, 3, offsetof(fence_vertex, color));
    describe_attribute(uv_location, 2, offsetof(fence_vertex, uv));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    shader_.use();
    shader_.set_int("u_net", net_texture_unit);
    return true;
}

void fence_renderer::shutdown() {
    net_.shutdown();
    buffer_.shutdown();
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    shader_.shutdown();
    uploaded_ = false;
}

bool fence_renderer::begin(const render_fences* fences, const glm::mat4& view_proj, const glm::vec3& light_direction,
                           const scene_haze& haze, frame_profile* profile) {
    if (fences == nullptr || (fences->posts.empty() && fences->nets.empty()) || shader_.id() == 0 || vao_ == 0) {
        return false;
    }
    if (!uploaded_ || uploaded_revision_ != fences->revision) {
        // Posts first, then nets, in one buffer.
        std::vector<fence_vertex> vertices = fences->posts;
        vertices.insert(vertices.end(), fences->nets.begin(), fences->nets.end());
        buffer_.upload(vertices.data(), vertices.size() * sizeof(fence_vertex), profile);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        post_vertex_count_ = fences->posts.size();
        net_vertex_count_ = fences->nets.size();
        uploaded_revision_ = fences->revision;
        uploaded_ = true;
    }
    shader_.set_profile(profile);
    shader_.use();
    shader_.set_mat4("u_view_proj", view_proj);
    shader_.set_vec3("u_light_dir", light_direction);
    set_scene_haze_uniforms(shader_, haze);
    net_.bind(net_texture_unit);
    glBindVertexArray(vao_);
    return true;
}

void fence_renderer::draw_posts(const render_fences* fences, const glm::mat4& view_proj, const glm::vec3& light_direction,
                                const scene_haze& haze, frame_profile* profile) {
    if (!begin(fences, view_proj, light_direction, haze, profile) || post_vertex_count_ == 0) {
        glBindVertexArray(0);
        return;
    }
    shader_.set_float("u_textured", 0.0f);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(post_vertex_count_));
    record_draw_call(profile);
    glBindVertexArray(0);
}

void fence_renderer::draw_nets(const render_fences* fences, const glm::mat4& view_proj, const glm::vec3& light_direction,
                               const scene_haze& haze, frame_profile* profile) {
    if (!begin(fences, view_proj, light_direction, haze, profile) || net_vertex_count_ == 0) {
        glBindVertexArray(0);
        return;
    }
    shader_.set_float("u_textured", 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDrawArrays(GL_TRIANGLES, static_cast<GLint>(post_vertex_count_), static_cast<GLsizei>(net_vertex_count_));
    record_draw_call(profile);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}
