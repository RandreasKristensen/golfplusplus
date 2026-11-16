#include "renderer/terrain_render_mesh.h"

#include "renderer/render_mesh_chunks.h"
#include "renderer/terrain_palette.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

namespace {
// How far under a pond's surface its floor is all mud where the patches are
// thinnest, and how wide the patches of grass and mud are.
constexpr float pond_bed_fade_meters = 1.5f;
constexpr float pond_bed_patch_meters = 3.0f;

// Each hole's line as segments, from its ribbon's rows (their first and
// last points' middle), every few rows: what fairways and greens are mown
// along.
constexpr int mow_line_row_step = 4;

std::vector<std::array<glm::vec2, 2>> hole_lines(const play_area& area) {
    std::vector<std::array<glm::vec2, 2>> lines;
    for (const terrain_mesh& hole : area.holes) {
        const int across = hole.cross_section_count;
        if (across <= 0 || hole.section_count < 2) {
            continue;
        }
        const auto middle = [&](const int row) {
            const glm::vec3& first = hole.vertices[static_cast<std::size_t>(row * across)].position;
            const glm::vec3& last = hole.vertices[static_cast<std::size_t>(row * across + across - 1)].position;
            return glm::vec2(first.x + last.x, first.z + last.z) * 0.5f;
        };
        for (int row = 0; row < hole.section_count - 1; row += mow_line_row_step) {
            const int next = std::min(row + mow_line_row_step, hole.section_count - 1);
            lines.push_back({middle(row), middle(next)});
        }
    }
    return lines;
}

// The nearest line to a point: the way along it (none when there is no
// line) and how far across it the point lies, signed.
struct line_place {
    glm::vec2 way{0.0f};
    float across = 0.0f;
};

line_place nearest_line(const std::vector<std::array<glm::vec2, 2>>& lines, const glm::vec2& point) {
    float best = std::numeric_limits<float>::max();
    line_place place;
    for (const auto& [a, b] : lines) {
        const glm::vec2 ab = b - a;
        const float length_squared = glm::dot(ab, ab);
        if (length_squared < 1.0e-6f) {
            continue;
        }
        const float t = std::clamp(glm::dot(point - a, ab) / length_squared, 0.0f, 1.0f);
        const glm::vec2 offset = point - (a + ab * t);
        const float distance_squared = glm::dot(offset, offset);
        if (distance_squared < best) {
            best = distance_squared;
            place.way = ab / std::sqrt(length_squared);
            place.across = place.way.x * offset.y - place.way.y * offset.x;
        }
    }
    return place;
}

bool baked(const height_grid& grid) {
    return grid.columns > 0 && grid.rows > 0;
}

// 0..1, fixed for a whole-number cell.
float cell_hash(const int x, const int z) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8da6b343u ^ static_cast<std::uint32_t>(z) * 0xd8163841u;
    h = (h ^ (h >> 13)) * 0x5bd1e995u;
    return static_cast<float>((h ^ (h >> 15)) & 0xffffu) / 65535.0f;
}

// Smooth 0..1 noise over cells one unit across.
float value_noise(const float x, const float z) {
    const float fx = std::floor(x);
    const float fz = std::floor(z);
    const int cx = static_cast<int>(fx);
    const int cz = static_cast<int>(fz);
    const float tx = (x - fx) * (x - fx) * (3.0f - 2.0f * (x - fx));
    const float tz = (z - fz) * (z - fz) * (3.0f - 2.0f * (z - fz));
    const float near_row = glm::mix(cell_hash(cx, cz), cell_hash(cx + 1, cz), tx);
    const float far_row = glm::mix(cell_hash(cx, cz + 1), cell_hash(cx + 1, cz + 1), tx);
    return glm::mix(near_row, far_row, tz);
}

// How much of the floor at `position`, `depth` under its pond's surface, is
// mud, 0..1: none at the water's edge, deeper in sooner or later by patch.
float pond_bed_share(const glm::vec3& position, const float depth) {
    const float x = position.x / pond_bed_patch_meters;
    const float z = position.z / pond_bed_patch_meters;
    const float patch = 0.65f * value_noise(x, z) + 0.35f * value_noise(2.7f * x + 19.0f, 2.7f * z + 7.0f);
    const float share = std::clamp(depth / pond_bed_fade_meters * (0.4f + 1.2f * patch), 0.0f, 1.0f);
    return share * share * (3.0f - 2.0f * share);
}
}

render_terrain_vertex lit_ground_vertex(const glm::vec3& position, const glm::vec3& normal, const glm::vec3& color,
                                        const float rough, const ground_light_bake& light) {
    render_terrain_vertex out;
    out.position = position;
    out.normal = normal;
    out.color = color;
    out.map_color = color;
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
                                            const ground_colors& colors, const ground_light_bake& light,
                                            const play_area* ponds, const bool mow_stripes) {
    render_static_mesh render;
    render.revision = revision;
    const std::vector<std::array<glm::vec2, 2>> lines =
        ponds != nullptr && mow_stripes ? hole_lines(*ponds) : std::vector<std::array<glm::vec2, 2>>{};
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
                const float mud = level ? pond_bed_share(vertex.position, std::max(0.0f, *level - vertex.position.y)) : 0.0f;
                render.vertices.push_back(
                    lit_ground_vertex(vertex.position, vertex.normal, glm::mix(colors.rough, colors.pond_bed, mud), 1.0f - mud, light));
                render.vertices.back().blades = 1.0f - mud;
                render.vertices.back().map_color = colors.water;
                continue;
            }
            const bool heavy = vertex.material == terrain_material::heavy_rough;
            render.vertices.push_back(lit_ground_vertex(
                vertex.position, vertex.normal,
                terrain_material_color(colors, vertex.material, std::min(1.0f, std::abs(vertex.distance_from_center) / half_width)),
                vertex.material == terrain_material::rough || heavy ? 1.0f : 0.0f, light));
            render.vertices.back().blades = vertex.material == terrain_material::rough ? 1.0f : heavy ? 2.0f : 0.0f;
            const bool fairway = vertex.material == terrain_material::fairway;
            if (!lines.empty() && (fairway || vertex.material == terrain_material::green)) {
                const line_place place = nearest_line(lines, glm::vec2(vertex.position.x, vertex.position.z));
                render.vertices.back().mow = place.way * (fairway ? 2.0f : 1.0f);
                render.vertices.back().mow_across = place.across;
            }
        }
        for (const std::uint32_t index : mesh->indices) {
            render.indices.push_back(offset + index);
        }
    }
    render.bounds = compute_render_mesh_bounds(render.vertices);
    render.chunks = build_render_mesh_chunks(render.vertices, render.indices);
    return render;
}
