#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "renderer/frustum.h"
#include "renderer/render_mesh.h"

// GL-free render chunking and chunk culling for static indexed meshes
// (course terrain + apron, material overlays).
//
// Chunking scheme: order-preserving spatial runs. Triangles are walked in
// index-buffer order and a new chunk starts whenever adding the next triangle
// would grow the current chunk's XZ bounds past `target_extent` on either axis.
// The index buffer is never reordered, so:
//   - every chunk is a contiguous index range and the chunks tile the buffer,
//   - the triangle draw order is exactly the authored order. This matters:
//     overlapping, coplanar material overlay zones (and overlapping hole/apron
//     terrain in the hub) resolve with GL_LESS by draw order, so reordering
//     triangles by a spatial grid could flip which surface wins.
// Course meshes are emitted as spline section rows, apron grid rows and one
// zone at a time, so consecutive triangles are spatially coherent and the runs
// come out as compact pieces of each hole.

struct render_chunk_settings {
    // Largest XZ size of one chunk, in world units (meters). A single triangle
    // larger than this still forms one chunk. About two ribbon widths: small
    // enough that walking around a hub culls most of the terrain.
    float target_extent = 48.0f;
    // Upper bound on chunk count. If a mesh would produce more (e.g. input that
    // is not spatially coherent), `target_extent` is doubled until it fits, so
    // culling degrades gracefully instead of the chunk list exploding.
    std::size_t max_chunks = 512;
};

// Builds chunks over whole triangles of `indices`. Trailing indices that do not
// form a full triangle are left out (GL_TRIANGLES never draws them). Triangles
// referencing out-of-range vertices stay in their chunk but do not contribute
// to its bounds; a chunk whose bounds end up invalid is never culled.
// Returns no chunks for an empty mesh.
std::vector<render_mesh_chunk> build_render_mesh_chunks(const std::vector<render_terrain_vertex>& vertices,
                                                        const std::vector<std::uint32_t>& indices,
                                                        const render_chunk_settings& settings = render_chunk_settings{});

struct render_index_range {
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
};

// Per-mesh culling result for one frame. Plain data so callers can fold it into
// profiling output.
struct render_chunk_cull_stats {
    std::uint32_t chunks_total = 0;
    std::uint32_t chunks_visible = 0;
    std::uint32_t chunks_culled = 0;
    // glDrawElements calls needed for this mesh (== ranges.size()).
    std::uint32_t draw_ranges = 0;
    std::uint64_t indices_total = 0;
    // Includes indices of culled chunks that were merged into a range to stay
    // under the range limit.
    std::uint64_t indices_drawn = 0;
};

// Appends [first_index, first_index + index_count) to `ranges`, extending the
// last range instead when the two are contiguous. Empty ranges are ignored.
void append_render_index_range(std::vector<render_index_range>& ranges,
                               std::uint32_t first_index,
                               std::uint32_t index_count);

// While there are more than `max_ranges` ranges (min 1), joins the neighbouring
// pair separated by the smallest index gap. The gap is then drawn too, which is
// always safe (it only draws geometry the GPU clips) and trades a few culled
// indices for fewer draw calls. Expects sorted, non-overlapping ranges.
void limit_render_index_ranges(std::vector<render_index_range>& ranges, std::size_t max_ranges);

// Rebuilds `ranges` (cleared first, capacity kept) with the merged index ranges
// of the chunks flagged in `visible` (one flag per chunk; missing flags count
// as visible). Chunk ranges are clamped to `index_count`. With no chunks, a
// non-empty mesh is drawn as one range.
render_chunk_cull_stats collect_chunk_index_ranges(const std::vector<render_mesh_chunk>& chunks,
                                                   const std::vector<bool>& visible,
                                                   std::size_t index_count,
                                                   std::size_t max_ranges,
                                                   std::vector<render_index_range>& ranges);

// Same, with visibility from a conservative frustum test of each chunk's bounds.
render_chunk_cull_stats collect_visible_index_ranges(const std::vector<render_mesh_chunk>& chunks,
                                                     const view_frustum& frustum,
                                                     std::size_t index_count,
                                                     std::size_t max_ranges,
                                                     std::vector<render_index_range>& ranges);
