#include "renderer/terrain_render_mesh.h"

#include "renderer/render_mesh_chunks.h"
#include "renderer/render_mesh_lod.h"
#include "renderer/terrain_palette.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>

#include <glm/common.hpp>

namespace {
// How far under a pond's surface its floor is all mud: on its straight bank
// (physics/terrain.h) a hand's width down, a little way in from its outline.
constexpr float pond_bed_fade_meters = 0.12f;

bool baked(const height_grid& grid) {
    return grid.columns > 0 && grid.rows > 0;
}

// How much of the floor `depth` under its pond's surface is mud, 0..1.
float pond_bed_share(const float depth) {
    const float share = std::clamp(depth / pond_bed_fade_meters, 0.0f, 1.0f);
    return share * share * (3.0f - 2.0f * share);
}
}

render_terrain_vertex lit_ground_vertex(const glm::vec3& position, const glm::vec3& normal, const glm::vec3& color,
                                        const float rough, const ground_light_bake& light) {
    render_terrain_vertex out;
    out.position = position;
    out.set_normal(normal);
    out.set_color(color);
    out.set_rough(rough);
    out.set_ao(baked(light.sky_visibility) ? sample_height_grid(light.sky_visibility, position.x, position.z) : 1.0f);
    for (std::size_t way = 0; way < out.horizon.size(); ++way) {
        const height_grid& horizon = light.horizons[way];
        const float sine = baked(horizon) ? sample_height_grid(horizon, position.x, position.z) : 0.0f;
        out.horizon[way] = static_cast<std::uint8_t>(std::lround(std::clamp(sine, 0.0f, 1.0f) * 255.0f));
    }
    return out;
}

render_static_mesh make_terrain_render_mesh(const std::vector<const terrain_mesh*>& meshes, const std::uint64_t revision,
                                            const ground_colors& colors, const ground_light_bake& light,
                                            const play_area* ponds) {
    render_static_mesh render;
    render.revision = revision;
    for (const terrain_mesh* mesh : meshes) {
        if (mesh == nullptr || mesh->vertices.empty() || mesh->indices.empty()) {
            continue;
        }
        const std::uint32_t offset = static_cast<std::uint32_t>(render.vertices.size());
        const float half_width = std::max(0.001f, mesh->width * 0.5f);
        for (const terrain_vertex& vertex : mesh->vertices) {
            if (ponds != nullptr && vertex.material == terrain_material::water) {
                // On the outline itself no pond is found: the water's edge, all grass.
                const std::optional<float> level = water_level_at(*ponds, vertex.position);
                const float mud = level ? pond_bed_share(*level - vertex.position.y) : 0.0f;
                render.vertices.push_back(
                    lit_ground_vertex(vertex.position, vertex.normal, glm::mix(colors.rough, colors.pond_bed, mud), 1.0f - mud, light));
                // The bed keeps its grass colour at the bank, but no rough
                // blades grow within the pond's exact outline.
                render.vertices.back().set_blades(0.0f);
                continue;
            }
            const bool heavy = vertex.material == terrain_material::heavy_rough;
            const glm::vec3 color =
                terrain_material_color(colors, vertex.material, std::min(1.0f, std::abs(vertex.distance_from_center) / half_width));
            render.vertices.push_back(
                lit_ground_vertex(vertex.position, vertex.normal, color, vertex.material == terrain_material::rough || heavy ? 1.0f : 0.0f, light));
            render.vertices.back().set_blades(vertex.material == terrain_material::rough ? 1.0f : heavy ? 2.0f : 0.0f);
            render.vertices.back().set_turf(vertex.material == terrain_material::fairway ? 1.0f : vertex.material == terrain_material::green ? 2.0f : 0.0f);
        }
        for (const std::uint32_t index : mesh->indices) {
            render.indices.push_back(offset + index);
        }
    }
    render.bounds = compute_render_mesh_bounds(render.vertices);
    render.chunks = build_render_mesh_chunks(render.vertices, render.indices);
    return render;
}

render_static_mesh make_ground_render_mesh(const play_area& area, const std::uint64_t revision, const ground_colors& colors,
                                           const ground_light_bake& light, const terrain_lod_style& lod) {
    terrain_mesh ground;
    ground.vertices = area.ground.vertices;
    ground.indices = area.ground.indices;
    ground.width = area.ground.width;
    for (terrain_vertex& vertex : ground.vertices) {
        if (vertex.material != terrain_material::water) {
            const std::optional<terrain_sample> on_hole = sample_holes(area.holes, vertex.position);
            vertex.material = on_hole ? on_hole->material : terrain_material::rough;
        }
    }
    render_static_mesh mesh = make_terrain_render_mesh({&ground}, revision, colors, light, &area);
    build_render_mesh_lod(mesh, lod);
    return mesh;
}
