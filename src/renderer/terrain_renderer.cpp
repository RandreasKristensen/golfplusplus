#include "renderer/terrain_renderer.h"

#include "renderer/gl_loader.h"

#include <cstddef>

namespace {
// Frustum-culled chunk ranges become draws in index order, so overlay triangles
// keep the order they lie in. Two ranges less than a draw call's cost in
// indices apart are one draw (culled chunks between them drawn, not skipped):
// measured at about 0.5 ns an index against 3 us a draw, 6144 indices. The
// maxima only bound a pathological view; a map of 780 chunks seen from its
// middle needs about 150.
constexpr std::uint32_t draw_merge_gap_indices = 6144;
constexpr std::size_t max_terrain_draw_ranges = 256;
constexpr std::size_t max_material_overlay_draw_ranges = 64;
// The drawn zone shapes sit a few centimetres over the ground; this pulls them
// further forward in depth so far-off ones never flicker under it.
constexpr float material_overlay_offset_factor = -1.0f;
constexpr float material_overlay_offset_units = -2.0f;

void attribute(const GLuint location, const GLint size, const GLenum type, const bool normalized, const std::size_t offset) {
    glEnableVertexAttribArray(location);
    glVertexAttribPointer(location, size, type, normalized ? GL_TRUE : GL_FALSE, sizeof(render_terrain_vertex),
                          reinterpret_cast<void*>(offset));
}

}

void describe_render_terrain_vertex() {
    attribute(0, 3, GL_FLOAT, false, offsetof(render_terrain_vertex, position));
    attribute(1, 4, GL_INT_2_10_10_10_REV, true, offsetof(render_terrain_vertex, normal_bits));
    attribute(2, 4, GL_UNSIGNED_BYTE, true, offsetof(render_terrain_vertex, color_rough));
    // Paved, sky, turf and blades as raw bytes: turf and blades count in
    // steps of 1/127 (render_terrain_vertex::level_steps), so the shader divides.
    attribute(3, 4, GL_UNSIGNED_BYTE, false, offsetof(render_terrain_vertex, paved_ao_turf_blades));
    // The horizon's 8 ways as two normalized byte quads.
    attribute(4, 4, GL_UNSIGNED_BYTE, true, offsetof(render_terrain_vertex, horizon));
    attribute(5, 4, GL_UNSIGNED_BYTE, true, offsetof(render_terrain_vertex, horizon) + 4);
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
        storage_.delete_buffer(buffers->lod_ebo);
        storage_.delete_buffer(buffers->ebo);
        storage_.delete_buffer(buffers->vbo);
        if (buffers->vao != 0) {
            glDeleteVertexArrays(1, &buffers->vao);
        }
        if (buffers->lod_vao != 0) {
            glDeleteVertexArrays(1, &buffers->lod_vao);
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
    storage_.buffer(buffers.vbo, GL_ARRAY_BUFFER, vertex_bytes, mesh->vertices.data(), GL_STATIC_DRAW, profile);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers.ebo);
    storage_.buffer(buffers.ebo, GL_ELEMENT_ARRAY_BUFFER, index_bytes, mesh->indices.data(), GL_STATIC_DRAW, profile);
    glBindVertexArray(0);
    if (mesh->lod_indices.empty()) {
        storage_.delete_buffer(buffers.lod_ebo);
        return;
    }
    if (buffers.lod_vao == 0) {
        glGenVertexArrays(1, &buffers.lod_vao);
    }
    if (buffers.lod_ebo == 0) {
        glGenBuffers(1, &buffers.lod_ebo);
    }
    glBindVertexArray(buffers.lod_vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffers.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers.lod_ebo);
    describe_render_terrain_vertex();
    storage_.buffer(buffers.lod_ebo, GL_ELEMENT_ARRAY_BUFFER, mesh->lod_indices.size() * sizeof(std::uint32_t),
                    mesh->lod_indices.data(), GL_STATIC_DRAW, profile);
    glBindVertexArray(0);
}

terrain_cull_stats terrain_renderer::draw(const shader_program& shader, const render_static_mesh* terrain,
                                          const render_static_mesh* material_overlay, const glm::mat4& view_proj,
                                          const view_frustum& frustum, const overlay_depth overlay,
                                          const render_lod_view& lod, frame_profile* profile) {
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
        const render_lod_view* lod;
    } meshes[] = {
        {terrain_, terrain, max_terrain_draw_ranges, terrain_ranges_, stats.terrain, false, &lod},
        {material_overlay_, material_overlay, max_material_overlay_draw_ranges, material_overlay_ranges_,
         stats.material_overlay, true, nullptr},
    };
    for (const auto& mesh : meshes) {
        if (mesh.buffers.index_count <= 0 || mesh.mesh == nullptr) {
            continue;
        }
        mesh.stats = collect_visible_index_ranges(mesh.mesh->chunks, frustum, static_cast<std::size_t>(mesh.buffers.index_count),
                                                  mesh.max_ranges, mesh.ranges, draw_merge_gap_indices, mesh.lod,
                                                  &mesh.mesh->lod_error_meters);
        if (profile) {
            auto& work = profile->work.current();
            work.terrain_indices_visible += mesh.stats.indices_visible;
            work.terrain_indices_submitted += mesh.stats.indices_drawn;
        }
        if (mesh.ranges.empty()) {
            continue;
        }
        set_terrain_draw_state(shader, glm::mat4(1.0f), view_proj, glm::vec3(1.0f), 1.0f, 0.0f, true);
        shader.set_float("u_lift_per_depth", mesh.pulled_forward && terrain != nullptr && !terrain->lod_error_meters.empty() ? lod.lift_per_depth : 0.0f);
        std::uint8_t bound_level = 0;
        glBindVertexArray(mesh.buffers.vao);
        if (mesh.pulled_forward && overlay == overlay_depth::over_all) {
            glDepthFunc(GL_ALWAYS);
        } else if (mesh.pulled_forward) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(material_overlay_offset_factor, material_overlay_offset_units);
        }
        for (const render_index_range& range : mesh.ranges) {
            if ((range.level > 0) != (bound_level > 0)) {
                glBindVertexArray(range.level > 0 ? mesh.buffers.lod_vao : mesh.buffers.vao);
            }
            bound_level = range.level;
            const std::uintptr_t offset = static_cast<std::uintptr_t>(range.first_index) * sizeof(std::uint32_t);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(range.index_count), GL_UNSIGNED_INT,
                           reinterpret_cast<void*>(offset));
            record_draw_call(shader.profile());
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDepthFunc(GL_LESS);
    }
    glBindVertexArray(0);
    return stats;
}
