#include "renderer/tree_renderer.h"

#include <SDL.h>

#include <glm/vec3.hpp>

#include <cstddef>
#include <type_traits>

#include "core/gl_loader.h"

namespace {
// Instancing entry points are core since GL 3.1/3.3 but are not part of the
// base loader, so they are fetched here like gl_timer_pool does for queries.
using vertex_attrib_divisor_fn = void (APIENTRY*)(GLuint, GLuint);
using draw_arrays_instanced_fn = void (APIENTRY*)(GLenum, GLint, GLsizei, GLsizei);

constexpr GLuint position_location = 0;
constexpr GLuint normal_location = 1;
constexpr GLuint instance_offset_location = 3;
constexpr GLuint instance_scale_location = 4;

// Same flat colors the per-tree draws used.
const glm::vec3 trunk_color(0.31f, 0.20f, 0.11f);
const glm::vec3 leaf_color(0.06f, 0.24f, 0.11f);

void* load_proc(const char* name) {
    return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
}
}

// The instance buffer is uploaded as raw floats: offset.xyz then scale.xyz.
static_assert(sizeof(render_tree_instance) == 6 * sizeof(float), "render_tree_instance must be six packed floats");
static_assert(offsetof(render_tree_instance, scale) == 3 * sizeof(float), "render_tree_instance::scale must follow offset");
static_assert(std::is_standard_layout<render_tree_instance>::value, "render_tree_instance must be standard layout");

bool tree_renderer::init(const char* vertex_path,
                         const char* fragment_path,
                         const mesh_source trunk_mesh,
                         const mesh_source leaf_mesh) {
    shutdown();

    vertex_attrib_divisor_ = load_proc("glVertexAttribDivisor");
    draw_arrays_instanced_ = load_proc("glDrawArraysInstanced");
    if (vertex_attrib_divisor_ == nullptr || draw_arrays_instanced_ == nullptr) {
        SDL_Log("Instanced rendering entry points unavailable; tree rendering requires OpenGL 3.3.");
        shutdown();
        return false;
    }

    if (!shader_.load_from_files(vertex_path, fragment_path)) {
        shutdown();
        return false;
    }

    if (!init_part(trunks_, trunk_mesh) || !init_part(leaves_, leaf_mesh)) {
        shutdown();
        return false;
    }

    return true;
}

void tree_renderer::shutdown() {
    shutdown_part(trunks_);
    shutdown_part(leaves_);
    shader_.shutdown();
    uploaded_revision_ = 0;
    uploaded_tree_count_ = 0;
    uploaded_ = false;
    vertex_attrib_divisor_ = nullptr;
    draw_arrays_instanced_ = nullptr;
}

bool tree_renderer::init_part(instanced_part& part, const mesh_source mesh) {
    if (mesh.vbo == 0 || mesh.vertex_count <= 0) {
        SDL_Log("Tree renderer needs a valid shared mesh buffer.");
        return false;
    }

    const auto divisor = reinterpret_cast<vertex_attrib_divisor_fn>(vertex_attrib_divisor_);

    part.vertex_count = mesh.vertex_count;
    glGenVertexArrays(1, &part.vao);
    glGenBuffers(1, &part.instance_vbo);
    if (part.vao == 0 || part.instance_vbo == 0) {
        SDL_Log("Tree renderer failed to allocate GL objects.");
        return false;
    }

    glBindVertexArray(part.vao);

    // Shared unit mesh: interleaved position + normal, same layout as the
    // renderer's own cylinder/cone VAOs.
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glEnableVertexAttribArray(position_location);
    glVertexAttribPointer(position_location, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(normal_location);
    glVertexAttribPointer(normal_location, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));

    glBindBuffer(GL_ARRAY_BUFFER, part.instance_vbo);
    glEnableVertexAttribArray(instance_offset_location);
    glVertexAttribPointer(instance_offset_location,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(render_tree_instance),
                          reinterpret_cast<void*>(offsetof(render_tree_instance, offset)));
    divisor(instance_offset_location, 1);
    glEnableVertexAttribArray(instance_scale_location);
    glVertexAttribPointer(instance_scale_location,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(render_tree_instance),
                          reinterpret_cast<void*>(offsetof(render_tree_instance, scale)));
    divisor(instance_scale_location, 1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void tree_renderer::shutdown_part(instanced_part& part) {
    if (part.instance_vbo != 0) {
        glDeleteBuffers(1, &part.instance_vbo);
        part.instance_vbo = 0;
    }

    if (part.vao != 0) {
        glDeleteVertexArrays(1, &part.vao);
        part.vao = 0;
    }

    part.vertex_count = 0;
    part.instance_count = 0;
}

void tree_renderer::upload_part(instanced_part& part,
                                const std::vector<render_tree_instance>& instances,
                                frame_profile* profile) {
    part.instance_count = instances.size();
    if (instances.empty() || part.instance_vbo == 0) {
        return;
    }

    const std::size_t bytes = instances.size() * sizeof(render_tree_instance);
    glBindBuffer(GL_ARRAY_BUFFER, part.instance_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), instances.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    record_buffer_upload(profile, bytes);
}

void tree_renderer::draw_part(const instanced_part& part, frame_profile* profile) const {
    if (part.vao == 0 || part.instance_count == 0 || part.vertex_count <= 0) {
        return;
    }

    glBindVertexArray(part.vao);
    reinterpret_cast<draw_arrays_instanced_fn>(draw_arrays_instanced_)(GL_TRIANGLES,
                                                                       0,
                                                                       static_cast<GLsizei>(part.vertex_count),
                                                                       static_cast<GLsizei>(part.instance_count));
    record_draw_call(profile);
}

void tree_renderer::draw(const std::vector<render_tree>& trees,
                         const std::uint64_t revision,
                         const glm::mat4& view,
                         const glm::mat4& proj,
                         frame_profile* profile) {
    if (shader_.id() == 0 || draw_arrays_instanced_ == nullptr) {
        return;
    }

    if (!uploaded_ || uploaded_revision_ != revision || uploaded_tree_count_ != trees.size()) {
        const render_tree_instance_batch batch = build_tree_instances(trees);
        upload_part(trunks_, batch.trunks, profile);
        upload_part(leaves_, batch.leaves, profile);
        uploaded_revision_ = revision;
        uploaded_tree_count_ = trees.size();
        uploaded_ = true;
    }

    if (trunks_.instance_count == 0 && leaves_.instance_count == 0) {
        return;
    }

    shader_.set_profile(profile);
    shader_.use();
    shader_.set_mat4("u_view_proj", proj * view);
    // Flat-color path of terrain.frag, exactly like the old per-tree draws.
    shader_.set_int("u_use_vertex_color", 0);
    shader_.set_float("u_alpha", 1.0f);

    shader_.set_vec3("u_color", trunk_color);
    draw_part(trunks_, profile);

    shader_.set_vec3("u_color", leaf_color);
    draw_part(leaves_, profile);

    glBindVertexArray(0);
}
