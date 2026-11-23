#include "renderer/render_mesh_lod.h"

#include "renderer/chunk_thinner.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

void build_render_mesh_lod(render_static_mesh& mesh, const terrain_lod_style& style) {
    mesh.lod_indices.clear();
    mesh.lod_error_meters.clear();
    for (render_mesh_chunk& chunk : mesh.chunks) {
        chunk.coarse = {};
    }
    if (style.levels <= 0 || mesh.chunks.empty()) {
        return;
    }
    const std::size_t levels = static_cast<std::size_t>(std::min(style.levels, max_terrain_lod_levels));
    for (std::size_t level = 0; level < levels; ++level) {
        mesh.lod_error_meters.push_back(style.finest_error_meters * std::pow(style.error_ratio, static_cast<float>(level)));
    }

    // A vertex used by two chunks is theirs both and never moves.
    std::vector<std::uint32_t> owner(mesh.vertices.size(), chunk_thinner::none);
    std::vector<std::uint8_t> shared(mesh.vertices.size(), 0);
    for (std::size_t c = 0; c < mesh.chunks.size(); ++c) {
        const render_mesh_chunk& chunk = mesh.chunks[c];
        for (std::uint32_t i = chunk.first_index; i < chunk.first_index + chunk.index_count; ++i) {
            const std::uint32_t g = mesh.indices[i];
            if (g >= owner.size()) {
                continue;
            }
            if (owner[g] == chunk_thinner::none) {
                owner[g] = static_cast<std::uint32_t>(c);
            } else if (owner[g] != c) {
                shared[g] = 1;
            }
        }
    }

    std::vector<std::vector<std::uint32_t>> lists(levels);
    std::vector<std::uint32_t> local_of(mesh.vertices.size(), chunk_thinner::none);
    chunk_thinner thinner(mesh.vertices, style.attribute_error);
    for (render_mesh_chunk& chunk : mesh.chunks) {
        if (!thinner.load(mesh.indices, chunk, shared, local_of)) {
            continue;
        }
        render_mesh_chunk::span previous;
        for (std::size_t level = 0; level < levels; ++level) {
            if (thinner.thin_to(mesh.lod_error_meters[level])) {
                previous.first = static_cast<std::uint32_t>(lists[level].size());
                thinner.append_alive(lists[level]);
                previous.count = static_cast<std::uint32_t>(lists[level].size()) - previous.first;
                previous.list = static_cast<std::uint8_t>(level + 1U);
            }
            chunk.coarse[level] = previous;
        }
    }
    std::vector<std::uint32_t> offsets(levels, 0);
    for (std::size_t level = 0; level < levels; ++level) {
        offsets[level] = static_cast<std::uint32_t>(mesh.lod_indices.size());
        mesh.lod_indices.insert(mesh.lod_indices.end(), lists[level].begin(), lists[level].end());
    }
    for (render_mesh_chunk& chunk : mesh.chunks) {
        for (render_mesh_chunk::span& span : chunk.coarse) {
            span.first = span.count > 0 ? span.first + offsets[span.list - 1U] : 0;
        }
    }
}

std::size_t choose_chunk_lod_level(const render_mesh_chunk& chunk, const std::vector<float>& error_meters,
                                   const render_lod_view& view) {
    if (view.max_error_pixels <= 0.0f || view.focal_pixels <= 0.0f || !chunk.bounds.valid) {
        return 0;
    }
    const glm::vec3 nearest = glm::clamp(view.eye, chunk.bounds.min, chunk.bounds.max);
    const float distance = std::max(glm::length(nearest - view.eye), 0.01f);
    for (std::size_t level = error_meters.size(); level > 0; --level) {
        if (chunk.coarse[level - 1U].count > 0 && error_meters[level - 1U] * view.focal_pixels / distance <= view.max_error_pixels) {
            return level;
        }
    }
    return 0;
}

render_lod_view make_render_lod_view(const glm::vec3& eye, const glm::mat4& projection, const int height,
                                    const float max_error_pixels) {
    render_lod_view view;
    view.eye = eye;
    view.focal_pixels = projection[1][1] * 0.5f * static_cast<float>(height);
    view.max_error_pixels = max_error_pixels;
    if (max_error_pixels > 0.0f && view.focal_pixels > 0.0f) {
        const float across = 1.0f / projection[0][0];
        const float up = 1.0f / projection[1][1];
        view.lift_per_depth = max_error_pixels / view.focal_pixels * std::sqrt(1.0f + across * across + up * up);
    }
    return view;
}
