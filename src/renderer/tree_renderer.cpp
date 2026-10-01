#include "renderer/tree_renderer.h"

#include <SDL.h>

#include <glm/vec3.hpp>

#include <cstddef>
#include <type_traits>

#include "renderer/gl_loader.h"

namespace {
constexpr GLuint position_location = 0;
constexpr GLuint instance_offset_location = 3;
constexpr GLuint instance_scale_location = 4;

const glm::vec3 trunk_color(0.31f, 0.20f, 0.11f);
const glm::vec3 leaf_color(0.06f, 0.24f, 0.11f);
}

// The instance buffer is uploaded as raw floats: offset.xyz then scale.xyz.
static_assert(sizeof(render_tree_instance) == 6 * sizeof(float), "render_tree_instance must be six packed floats");
static_assert(offsetof(render_tree_instance, scale) == 3 * sizeof(float), "render_tree_instance::scale must follow offset");
static_assert(std::is_standard_layout<render_tree_instance>::value, "render_tree_instance must be standard layout");

bool tree_renderer::init(const std::string& vertex_path,
                         const std::string& fragment_path,
                         const mesh_source trunk_mesh,
                         const mesh_source leaf_mesh) {
    shutdown();

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
}

bool tree_renderer::init_part(instanced_part& part, const mesh_source mesh) {
    if (mesh.vbo == 0 || mesh.vertex_count <= 0) {
        SDL_Log("Tree renderer needs a valid shared mesh buffer.");
        return false;
    }

    part.vertex_count = mesh.vertex_count;
    glGenVertexArrays(1, &part.vao);
    glGenBuffers(1, &part.instance_vbo);
    if (part.vao == 0 || part.instance_vbo == 0) {
        SDL_Log("Tree renderer failed to allocate GL objects.");
        return false;
    }

    glBindVertexArray(part.vao);

    // Shared unit mesh: interleaved position + normal; only the position is read.
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glEnableVertexAttribArray(position_location);
    glVertexAttribPointer(position_location, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, part.instance_vbo);
    glEnableVertexAttribArray(instance_offset_location);
    glVertexAttribPointer(instance_offset_location,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(render_tree_instance),
                          reinterpret_cast<void*>(offsetof(render_tree_instance, offset)));
    glVertexAttribDivisor(instance_offset_location, 1);
    glEnableVertexAttribArray(instance_scale_location);
    glVertexAttribPointer(instance_scale_location,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(render_tree_instance),
                          reinterpret_cast<void*>(offsetof(render_tree_instance, scale)));
    glVertexAttribDivisor(instance_scale_location, 1);

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
    glDrawArraysInstanced(GL_TRIANGLES, 0, static_cast<GLsizei>(part.vertex_count), static_cast<GLsizei>(part.instance_count));
    record_draw_call(profile);
}

bool tree_renderer::draw(const std::vector<tree_body>& trees,
                         const std::uint64_t revision,
                         const glm::mat4& view,
                         const glm::mat4& proj,
                         const view_frustum& frustum,
                         frame_profile* profile) {
    if (shader_.id() == 0) {
        return false;
    }

    if (!uploaded_ || uploaded_revision_ != revision || uploaded_tree_count_ != trees.size()) {
        const render_tree_instance_batch batch = build_tree_instances(trees);
        upload_part(trunks_, batch.trunks, profile);
        upload_part(leaves_, batch.leaves, profile);
        instance_bounds_ = compute_tree_instance_bounds(batch);
        uploaded_revision_ = revision;
        uploaded_tree_count_ = trees.size();
        uploaded_ = true;
    }

    if (trunks_.instance_count == 0 && leaves_.instance_count == 0) {
        return false;
    }

    // Whole-batch cull: skips both instanced draws when no tree is on screen.
    if (instance_bounds_.valid &&
        !frustum_intersects_aabb(frustum, instance_bounds_.min, instance_bounds_.max)) {
        return false;
    }

    shader_.set_profile(profile);
    shader_.use();
    shader_.set_mat4("u_view_proj", proj * view);

    shader_.set_vec3("u_color", trunk_color);
    draw_part(trunks_, profile);

    shader_.set_vec3("u_color", leaf_color);
    draw_part(leaves_, profile);

    glBindVertexArray(0);
    return true;
}
