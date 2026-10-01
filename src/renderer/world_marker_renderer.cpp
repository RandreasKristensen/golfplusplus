#include "renderer/world_marker_renderer.h"

#include <SDL.h>

#include <cstddef>
#include <type_traits>

#include "renderer/gl_loader.h"

namespace {
constexpr GLuint position_location = 0;
constexpr GLuint color_location = 1;

// Covers single-hole aiming (~1.7k vertices: tee, cup, flagstick, 28 aim
// dots, club) and a six-hole hub (~1.1k) without growing on early frames,
// plus the 1956-vertex golf cart on top of either.
constexpr std::size_t initial_vertex_capacity = 4096;
}

// The vertex buffer is uploaded as raw floats: position.xyz then color.rgba.
static_assert(sizeof(world_marker_vertex) == 7 * sizeof(float), "world_marker_vertex must be seven packed floats");
static_assert(offsetof(world_marker_vertex, color) == 3 * sizeof(float), "world_marker_vertex::color must follow position");
static_assert(std::is_standard_layout<world_marker_vertex>::value, "world_marker_vertex must be standard layout");

bool world_marker_renderer::init(const std::string& vertex_path, const std::string& fragment_path) {
    shutdown();

    if (!shader_.load_from_files(vertex_path, fragment_path)) {
        shutdown();
        return false;
    }

    glGenVertexArrays(1, &vao_);
    if (vao_ == 0) {
        SDL_Log("World marker renderer failed to allocate GL objects.");
        shutdown();
        return false;
    }

    glBindVertexArray(vao_);
    // init() leaves the buffer bound for the attribute pointers below.
    if (!buffer_.init(initial_vertex_capacity * sizeof(world_marker_vertex))) {
        SDL_Log("World marker renderer failed to allocate GL objects.");
        glBindVertexArray(0);
        shutdown();
        return false;
    }
    glEnableVertexAttribArray(position_location);
    glVertexAttribPointer(position_location,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(world_marker_vertex),
                          reinterpret_cast<void*>(offsetof(world_marker_vertex, position)));
    glEnableVertexAttribArray(color_location);
    glVertexAttribPointer(color_location,
                          4,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(world_marker_vertex),
                          reinterpret_cast<void*>(offsetof(world_marker_vertex, color)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void world_marker_renderer::shutdown() {
    buffer_.shutdown();

    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }

    shader_.shutdown();
}

void world_marker_renderer::draw(const world_marker_batch& batch,
                                 const glm::mat4& view_proj,
                                 frame_profile* profile) {
    if (batch.empty() || shader_.id() == 0 || vao_ == 0) {
        return;
    }

    buffer_.upload(batch.vertices().data(),
                   batch.vertices().size() * sizeof(world_marker_vertex),
                   profile);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    shader_.set_profile(profile);
    shader_.use();
    shader_.set_mat4("u_view_proj", view_proj);

    glBindVertexArray(vao_);
    for (const world_marker_run& run : batch.runs()) {
        if (run.count == 0) {
            continue;
        }
        glDepthMask(run.depth_write ? GL_TRUE : GL_FALSE);
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(run.first), static_cast<GLsizei>(run.count));
        record_draw_call(profile);
    }
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
}
