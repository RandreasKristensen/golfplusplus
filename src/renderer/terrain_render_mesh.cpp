#include "renderer/terrain_render_mesh.h"

#include "renderer/render_mesh_chunks.h"
#include "renderer/terrain_palette.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {
bool baked(const height_grid& grid) {
    return grid.columns > 0 && grid.rows > 0;
}
}

render_terrain_vertex lit_ground_vertex(const glm::vec3& position, const glm::vec3& normal, const glm::vec3& color,
                                        const float rough, const ground_light_bake& light) {
    render_terrain_vertex out;
    out.position = position;
    out.normal = normal;
    out.color = color;
    out.rough = rough;
    out.ao = baked(light.sky_visibility) ? sample_height_grid(light.sky_visibility, position.x, position.z) : 1.0f;
    for (std::size_t way = 0; way < out.horizon.size(); ++way) {
        const height_grid& horizon = light.horizons[way];
        const float sine = baked(horizon) ? sample_height_grid(horizon, position.x, position.z) : 0.0f;
        out.horizon[way] = static_cast<std::uint8_t>(std::lround(std::clamp(sine, 0.0f, 1.0f) * 255.0f));
    }
    return out;
}

render_static_mesh make_terrain_render_mesh(const std::vector<const terrain_mesh*>& meshes, const std::uint64_t revision,
                                            const ground_colors& colors, const ground_light_bake& light) {
    render_static_mesh render;
    render.revision = revision;
    for (const terrain_mesh* mesh : meshes) {
        if (mesh == nullptr || mesh->vertices.empty() || mesh->indices.empty()) {
            continue;
        }
        const std::uint32_t offset = static_cast<std::uint32_t>(render.vertices.size());
        const float half_width = std::max(0.001f, mesh->width * 0.5f);
        for (const terrain_vertex& vertex : mesh->vertices) {
            render.vertices.push_back(lit_ground_vertex(
                vertex.position, vertex.normal,
                terrain_material_color(colors, vertex.material, std::min(1.0f, std::abs(vertex.distance_from_center) / half_width)),
                vertex.material == terrain_material::rough ? 1.0f : 0.0f, light));
        }
        for (const std::uint32_t index : mesh->indices) {
            render.indices.push_back(offset + index);
        }
    }
    render.bounds = compute_render_mesh_bounds(render.vertices);
    render.chunks = build_render_mesh_chunks(render.vertices, render.indices);
    return render;
}
