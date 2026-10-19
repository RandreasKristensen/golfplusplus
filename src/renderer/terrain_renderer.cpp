#include "renderer/terrain_renderer.h"

#include "renderer/gl_loader.h"

#include <cstddef>

namespace {
// Frustum-culled chunk ranges are merged into at most this many draws per
// mesh; culled chunks inside a merged span are drawn rather than costing
// another draw call.
constexpr std::size_t max_terrain_draw_ranges = 4;
constexpr std::size_t max_material_overlay_draw_ranges = 2;
// The drawn zone shapes sit a few centimetres over the ground; this pulls them
// further forward in depth so far-off ones never flicker under it.
constexpr float material_overlay_offset_factor = -1.0f;
constexpr float material_overlay_offset_units = -2.0f;

void attribute(const GLuint location, const GLint size, const GLenum type, const bool normalized, const std::size_t offset) {
    glEnableVertexAttribArray(location);
    glVertexAttribPointer(location, size, type, normalized ? GL_TRUE : GL_FALSE, sizeof(render_terrain_vertex),
                          reinterpret_cast<void*>(offset));
}

void delete_buffer(unsigned int& buffer) {
    if (buffer != 0) {
        glDeleteBuffers(1, &buffer);
        buffer = 0;
    }
}
}

void describe_render_terrain_vertex() {
    attribute(0, 3, GL_FLOAT, false, offsetof(render_terrain_vertex, position));
    attribute(1, 3, GL_FLOAT, false, offsetof(render_terrain_vertex, normal));
    attribute(2, 3, GL_FLOAT, false, offsetof(render_terrain_vertex, color));
    attribute(3, 1, GL_FLOAT, false, offsetof(render_terrain_vertex, rough));
    attribute(4, 1, GL_FLOAT, false, offsetof(render_terrain_vertex, ao));
    // The horizon's 8 ways as two normalized byte quads.
    attribute(5, 4, GL_UNSIGNED_BYTE, true, offsetof(render_terrain_vertex, horizon));
    attribute(6, 4, GL_UNSIGNED_BYTE, true, offsetof(render_terrain_vertex, horizon) + 4);
}

void set_terrain_draw_state(const shader_program& shader, const glm::mat4& model, const glm::mat4& view_proj,
                            const glm::vec3& color, const float alpha, const float glow, const bool use_vertex_color) {
    shader.set_mat4("u_model", model);
    shader.set_mat4("u_mvp", view_proj * model);
    shader.set_vec3("u_color", color);
    shader.set_float("u_alpha", alpha);
    shader.set_float("u_glow", glow);
    shader.set_int("u_use_vertex_color", use_vertex_color ? 1 : 0);
}

bool terrain_renderer::init() {
    for (static_mesh_buffers* buffers : {&terrain_, &material_overlay_}) {
        glGenVertexArrays(1, &buffers->vao);
        glGenBuffers(1, &buffers->vbo);
        glGenBuffers(1, &buffers->ebo);
        glBindVertexArray(buffers->vao);
        glBindBuffer(GL_ARRAY_BUFFER, buffers->vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers->ebo);
        describe_render_terrain_vertex();
        glBindVertexArray(0);
    }
    return terrain_.vao != 0 && material_overlay_.vao != 0;
}

void terrain_renderer::shutdown() {
    for (static_mesh_buffers* buffers : {&terrain_, &material_overlay_}) {
        delete_buffer(buffers->ebo);
        delete_buffer(buffers->vbo);
        if (buffers->vao != 0) {
            glDeleteVertexArrays(1, &buffers->vao);
        }
        *buffers = static_mesh_buffers{};
    }
}

void terrain_renderer::upload(static_mesh_buffers& buffers, const render_static_mesh* mesh, frame_profile* profile) {
    if (mesh == nullptr) {
        buffers.index_count = 0;
        buffers.uploaded = false;
        return;
    }
    if (buffers.uploaded && buffers.uploaded_revision == mesh->revision) {
        return;
    }

    buffers.index_count = static_cast<int>(mesh->indices.size());
    buffers.uploaded = true;
    buffers.uploaded_revision = mesh->revision;
    if (mesh->vertices.empty() || mesh->indices.empty()) {
        buffers.index_count = 0;
        return;
    }

    const std::size_t vertex_bytes = mesh->vertices.size() * sizeof(render_terrain_vertex);
    const std::size_t index_bytes = mesh->indices.size() * sizeof(std::uint32_t);
    glBindVertexArray(buffers.vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffers.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertex_bytes), mesh->vertices.data(), GL_STATIC_DRAW);
    record_buffer_upload(profile, vertex_bytes);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(index_bytes), mesh->indices.data(), GL_STATIC_DRAW);
    record_buffer_upload(profile, index_bytes);
    glBindVertexArray(0);
}

terrain_cull_stats terrain_renderer::draw(const shader_program& shader, const render_static_mesh* terrain,
                                          const render_static_mesh* material_overlay, const glm::mat4& view_proj,
                                          const view_frustum& frustum, frame_profile* profile) {
    upload(terrain_, terrain, profile);
    upload(material_overlay_, material_overlay, profile);

    // The chunk lists were built with the meshes and describe the index buffer
    // uploaded for this revision.
    terrain_cull_stats stats;
    const struct {
        static_mesh_buffers& buffers;
        const render_static_mesh* mesh;
        std::size_t max_ranges;
        std::vector<render_index_range>& ranges;
        render_chunk_cull_stats& stats;
        bool pulled_forward;  // drawn over the ground it lies on
    } meshes[] = {
        {terrain_, terrain, max_terrain_draw_ranges, terrain_ranges_, stats.terrain, false},
        {material_overlay_, material_overlay, max_material_overlay_draw_ranges, material_overlay_ranges_,
         stats.material_overlay, true},
    };
    for (const auto& mesh : meshes) {
        if (mesh.buffers.index_count <= 0 || mesh.mesh == nullptr) {
            continue;
        }
        mesh.stats = collect_visible_index_ranges(mesh.mesh->chunks, frustum, static_cast<std::size_t>(mesh.buffers.index_count),
                                                  mesh.max_ranges, mesh.ranges);
        if (mesh.ranges.empty()) {
            continue;
        }
        set_terrain_draw_state(shader, glm::mat4(1.0f), view_proj, glm::vec3(1.0f), 1.0f, 0.0f, true);
        glBindVertexArray(mesh.buffers.vao);
        if (mesh.pulled_forward) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(material_overlay_offset_factor, material_overlay_offset_units);
        }
        for (const render_index_range& range : mesh.ranges) {
            const std::uintptr_t offset = static_cast<std::uintptr_t>(range.first_index) * sizeof(std::uint32_t);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(range.index_count), GL_UNSIGNED_INT,
                           reinterpret_cast<void*>(offset));
            record_draw_call(shader.profile());
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    glBindVertexArray(0);
    return stats;
}
