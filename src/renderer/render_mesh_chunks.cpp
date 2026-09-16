#include "renderer/render_mesh_chunks.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr int max_extent_doublings = 64;

void expand_bounds(render_mesh_bounds& bounds, const glm::vec3& point) {
    if (!bounds.valid) {
        bounds.min = point;
        bounds.max = point;
        bounds.valid = true;
        return;
    }
    bounds.min = glm::min(bounds.min, point);
    bounds.max = glm::max(bounds.max, point);
}

render_mesh_bounds merge_bounds(const render_mesh_bounds& a, const render_mesh_bounds& b) {
    if (!a.valid) {
        return b;
    }
    if (!b.valid) {
        return a;
    }
    render_mesh_bounds merged;
    merged.min = glm::min(a.min, b.min);
    merged.max = glm::max(a.max, b.max);
    merged.valid = true;
    return merged;
}

bool exceeds_xz_extent(const render_mesh_bounds& bounds, const float extent) {
    return bounds.valid &&
        (bounds.max.x - bounds.min.x > extent || bounds.max.z - bounds.min.z > extent);
}

render_mesh_bounds triangle_bounds(const std::vector<render_terrain_vertex>& vertices,
                                   const std::vector<std::uint32_t>& indices,
                                   const std::size_t first) {
    render_mesh_bounds bounds;
    for (std::size_t corner = 0; corner < 3U; ++corner) {
        const std::uint32_t index = indices[first + corner];
        if (index < vertices.size()) {
            expand_bounds(bounds, vertices[index].position);
        }
    }
    return bounds;
}

std::vector<render_mesh_chunk> chunk_with_extent(const std::vector<render_terrain_vertex>& vertices,
                                                 const std::vector<std::uint32_t>& indices,
                                                 const std::size_t triangle_index_count,
                                                 const float extent) {
    std::vector<render_mesh_chunk> chunks;
    render_mesh_chunk current;
    for (std::size_t i = 0; i < triangle_index_count; i += 3U) {
        const render_mesh_bounds triangle = triangle_bounds(vertices, indices, i);
        const render_mesh_bounds grown = merge_bounds(current.bounds, triangle);
        if (current.index_count > 0U && exceeds_xz_extent(grown, extent)) {
            chunks.push_back(current);
            current = render_mesh_chunk{};
            current.first_index = static_cast<std::uint32_t>(i);
            current.bounds = triangle;
        } else {
            current.bounds = grown;
        }
        current.index_count += 3U;
    }
    if (current.index_count > 0U) {
        chunks.push_back(current);
    }
    return chunks;
}

template <typename visible_fn>
render_chunk_cull_stats collect_ranges(const std::vector<render_mesh_chunk>& chunks,
                                       const visible_fn& is_visible,
                                       const std::size_t index_count,
                                       const std::size_t max_ranges,
                                       std::vector<render_index_range>& ranges) {
    ranges.clear();
    render_chunk_cull_stats stats;
    const std::uint32_t clamped_count = static_cast<std::uint32_t>(
        std::min<std::size_t>(index_count, std::numeric_limits<std::uint32_t>::max()));
    stats.indices_total = clamped_count;

    if (chunks.empty()) {
        append_render_index_range(ranges, 0U, clamped_count);
    } else {
        stats.chunks_total = static_cast<std::uint32_t>(chunks.size());
        for (std::size_t i = 0; i < chunks.size(); ++i) {
            const render_mesh_chunk& chunk = chunks[i];
            if (!is_visible(i)) {
                ++stats.chunks_culled;
                continue;
            }
            ++stats.chunks_visible;
            if (chunk.first_index >= clamped_count) {
                continue;
            }
            const std::uint32_t count = std::min(chunk.index_count, clamped_count - chunk.first_index);
            append_render_index_range(ranges, chunk.first_index, count);
        }
    }

    limit_render_index_ranges(ranges, max_ranges);
    stats.draw_ranges = static_cast<std::uint32_t>(ranges.size());
    for (const render_index_range& range : ranges) {
        stats.indices_drawn += range.index_count;
    }
    return stats;
}

} // namespace

std::vector<render_mesh_chunk> build_render_mesh_chunks(const std::vector<render_terrain_vertex>& vertices,
                                                        const std::vector<std::uint32_t>& indices,
                                                        const render_chunk_settings& settings) {
    const std::size_t triangle_index_count = std::min<std::size_t>(
        indices.size() - indices.size() % 3U,
        std::numeric_limits<std::uint32_t>::max() - 2U);
    if (vertices.empty() || triangle_index_count == 0U) {
        return {};
    }

    const std::size_t max_chunks = std::max<std::size_t>(1U, settings.max_chunks);
    float extent = settings.target_extent;
    if (!(extent > 0.0f) || !std::isfinite(extent)) {
        extent = 1.0f;
    }

    for (int attempt = 0; attempt < max_extent_doublings; ++attempt) {
        std::vector<render_mesh_chunk> chunks = chunk_with_extent(vertices, indices, triangle_index_count, extent);
        if (chunks.size() <= max_chunks) {
            return chunks;
        }
        extent *= 2.0f;
    }

    // Pathological input (e.g. non-finite positions): one chunk, never culled.
    render_mesh_chunk whole;
    whole.first_index = 0U;
    whole.index_count = static_cast<std::uint32_t>(triangle_index_count);
    return {whole};
}

void append_render_index_range(std::vector<render_index_range>& ranges,
                               const std::uint32_t first_index,
                               const std::uint32_t index_count) {
    if (index_count == 0U) {
        return;
    }
    if (!ranges.empty()) {
        render_index_range& last = ranges.back();
        if (last.first_index + last.index_count == first_index) {
            last.index_count += index_count;
            return;
        }
    }
    ranges.push_back(render_index_range{first_index, index_count});
}

void limit_render_index_ranges(std::vector<render_index_range>& ranges, const std::size_t max_ranges) {
    const std::size_t limit = std::max<std::size_t>(1U, max_ranges);
    while (ranges.size() > limit) {
        std::size_t best = 0;
        std::uint32_t best_gap = std::numeric_limits<std::uint32_t>::max();
        for (std::size_t i = 0; i + 1U < ranges.size(); ++i) {
            const std::uint32_t end = ranges[i].first_index + ranges[i].index_count;
            const std::uint32_t next = ranges[i + 1U].first_index;
            const std::uint32_t gap = next > end ? next - end : 0U;
            if (gap < best_gap) {
                best_gap = gap;
                best = i;
            }
        }
        const std::uint32_t merged_end = ranges[best + 1U].first_index + ranges[best + 1U].index_count;
        ranges[best].index_count = merged_end - ranges[best].first_index;
        ranges.erase(ranges.begin() + static_cast<std::ptrdiff_t>(best + 1U));
    }
}

render_chunk_cull_stats collect_chunk_index_ranges(const std::vector<render_mesh_chunk>& chunks,
                                                   const std::vector<bool>& visible,
                                                   const std::size_t index_count,
                                                   const std::size_t max_ranges,
                                                   std::vector<render_index_range>& ranges) {
    const auto is_visible = [&visible](const std::size_t i) {
        return i >= visible.size() || visible[i];
    };
    return collect_ranges(chunks, is_visible, index_count, max_ranges, ranges);
}

render_chunk_cull_stats collect_visible_index_ranges(const std::vector<render_mesh_chunk>& chunks,
                                                     const view_frustum& frustum,
                                                     const std::size_t index_count,
                                                     const std::size_t max_ranges,
                                                     std::vector<render_index_range>& ranges) {
    const auto is_visible = [&chunks, &frustum](const std::size_t i) {
        const render_mesh_bounds& bounds = chunks[i].bounds;
        return !bounds.valid || frustum_intersects_aabb(frustum, bounds.min, bounds.max);
    };
    return collect_ranges(chunks, is_visible, index_count, max_ranges, ranges);
}
