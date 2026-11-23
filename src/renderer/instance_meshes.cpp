#include "renderer/instance_meshes.h"

#include "renderer/gl_loader.h"

#include <SDL.h>

#include <cstddef>
#include <type_traits>
#include <vector>

namespace {
constexpr GLuint position_location = 0;
constexpr GLuint normal_location = 1;
constexpr GLuint axis_x_location = 3;
constexpr GLuint axis_y_location = 4;
constexpr GLuint axis_z_location = 5;
constexpr GLuint origin_location = 6;
constexpr GLuint tint_location = 7;
constexpr GLuint glow_location = 8;

void instance_attribute(const GLuint location, const GLint size, const std::size_t offset, const std::size_t first) {
    glEnableVertexAttribArray(location);
    glVertexAttribPointer(location, size, GL_FLOAT, GL_FALSE, sizeof(mesh_instance),
                          reinterpret_cast<void*>(first * sizeof(mesh_instance) + offset));
    glVertexAttribDivisor(location, 1);
}
}

// Uploaded as raw floats: the matrix's columns, colour and alpha, glow.
static_assert(sizeof(mesh_instance) == 17 * sizeof(float), "mesh_instance must be seventeen packed floats");
static_assert(offsetof(mesh_instance, color) == 12 * sizeof(float), "mesh_instance::color must follow the matrix");
static_assert(offsetof(mesh_instance, alpha) == 15 * sizeof(float), "mesh_instance::alpha must follow colour");
static_assert(std::is_standard_layout<mesh_instance>::value, "mesh_instance must be standard layout");

bool instance_meshes::init() {
    shutdown();
    const canonical_meshes meshes;
    std::vector<float> joined;
    for (const prop_mesh mesh : {prop_mesh::cylinder, prop_mesh::sphere, prop_mesh::quad}) {
        const std::size_t m = static_cast<std::size_t>(mesh);
        first_vertex_[m] = static_cast<int>(joined.size() / canonical_meshes::floats_per_vertex);
        vertex_count_[m] = static_cast<int>(meshes.vertex_count(mesh));
        joined.insert(joined.end(), meshes.vertices(mesh).begin(), meshes.vertices(mesh).end());
    }
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    // init() leaves the buffer bound for the attribute pointers below.
    if (vao_ == 0 || !buffer_.init(joined.size() * sizeof(float))) {
        SDL_Log("The instance meshes failed to allocate GL objects.");
        glBindVertexArray(0);
        shutdown();
        return false;
    }
    buffer_.upload(joined.data(), joined.size() * sizeof(float), nullptr, upload_kind::cached);
    constexpr GLsizei stride = static_cast<GLsizei>(canonical_meshes::floats_per_vertex * sizeof(float));
    glEnableVertexAttribArray(position_location);
    glVertexAttribPointer(position_location, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(normal_location);
    glVertexAttribPointer(normal_location, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void instance_meshes::shutdown() {
    buffer_.shutdown();
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
}

void instance_meshes::draw(const unsigned int instances, const prop_mesh mesh, const std::size_t first, const std::size_t count,
                           frame_profile* profile) const {
    if (vao_ == 0 || instances == 0 || count == 0) {
        return;
    }
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, instances);
    instance_attribute(axis_x_location, 3, offsetof(mesh_instance, axis_x), first);
    instance_attribute(axis_y_location, 3, offsetof(mesh_instance, axis_y), first);
    instance_attribute(axis_z_location, 3, offsetof(mesh_instance, axis_z), first);
    instance_attribute(origin_location, 3, offsetof(mesh_instance, origin), first);
    instance_attribute(tint_location, 4, offsetof(mesh_instance, color), first);
    instance_attribute(glow_location, 1, offsetof(mesh_instance, glow), first);
    const std::size_t m = static_cast<std::size_t>(mesh);
    glDrawArraysInstanced(GL_TRIANGLES, first_vertex_[m], vertex_count_[m], static_cast<GLsizei>(count));
    record_draw_call(profile);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}
