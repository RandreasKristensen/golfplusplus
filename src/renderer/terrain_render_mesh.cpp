#include "renderer/terrain_render_mesh.h"

#include "renderer/render_mesh_chunks.h"
#include "renderer/terrain_palette.h"

#include <algorithm>
#include <cmath>

render_static_mesh make_terrain_render_mesh(const std::vector<const terrain_mesh*>& meshes, const std::uint64_t revision) {
    render_static_mesh render;
    render.revision = revision;
    for (const terrain_mesh* mesh : meshes) {
        if (mesh == nullptr || mesh->vertices.empty() || mesh->indices.empty()) {
            continue;
        }
        const std::uint32_t offset = static_cast<std::uint32_t>(render.vertices.size());
        const float half_width = std::max(0.001f, mesh->width * 0.5f);
        for (const terrain_vertex& vertex : mesh->vertices) {
            render_terrain_vertex out;
            out.position = vertex.position;
            out.normal = vertex.normal;
            out.color = terrain_material_color(vertex.material, std::min(1.0f, std::abs(vertex.distance_from_center) / half_width));
            render.vertices.push_back(out);
        }
        for (const std::uint32_t index : mesh->indices) {
            render.indices.push_back(offset + index);
        }
    }
    render.bounds = compute_render_mesh_bounds(render.vertices);
    render.chunks = build_render_mesh_chunks(render.vertices, render.indices);
    return render;
}
