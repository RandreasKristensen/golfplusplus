#pragma once

// GL-free builder for the paper course map's terrain fill.
//
// The fill scan-converts every terrain triangle into map-space strip quads:
// 5k-11k quads for a full course, which used to be rebuilt and re-uploaded on
// every frame the map was open. It depends on nothing but the terrain mesh and
// the map layout, so the vertices are cached and only rebuilt when one of
// those actually changes (the layout normally does not change at all while the
// map is open: the ball and player positions that feed it sit inside the
// terrain bounds, which dominate them).
//
// Kept free of GL and render_data so the vertices can be unit tested against
// the uncached builder.

#include <cstddef>
#include <cstdint>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "renderer/overlay_batch.h"
#include "renderer/render_mesh.h"

struct course_map_layout {
    glm::vec2 center{0.0f, 0.02f};
    glm::vec2 half_size{0.68f, 0.76f};
    glm::vec3 world_center{0.0f};
    float scale = 0.01f;
};

// True when both layouts produce exactly the same map-space geometry.
bool course_map_layouts_match(const course_map_layout& a, const course_map_layout& b);

// World position -> overlay clip space. The paper map reads from the player's
// perspective, so world +X maps left.
glm::vec2 map_point(const course_map_layout& layout, const glm::vec3& position);

// Appends the clipped scan-converted strip quads of one map-space triangle.
void append_map_fill_triangle(overlay_batch& batch,
                              const course_map_layout& layout,
                              glm::vec2 a,
                              glm::vec2 b,
                              glm::vec2 c,
                              glm::vec3 color);

// Appends the fill for every triangle of `mesh` (null or empty: nothing).
void append_course_map_fill(overlay_batch& batch,
                            const course_map_layout& layout,
                            const render_static_mesh* mesh);

// Retained vertices for the fill plus the inputs they were built from.
struct course_map_fill_cache {
    overlay_batch fill;
    // Bumped on every rebuild; the GL side re-uploads only when it changes.
    std::uint64_t revision = 0;

    course_map_layout layout;
    const render_static_mesh* mesh = nullptr;
    std::uint64_t mesh_revision = 0;
    std::size_t mesh_vertex_count = 0;
    std::size_t mesh_index_count = 0;
    bool valid = false;
};

// Rebuilds `cache` only when the layout or the terrain mesh changed, and
// returns true when it did. The cached vertices are always identical to what
// append_course_map_fill would produce for the same inputs.
bool update_course_map_fill_cache(course_map_fill_cache& cache,
                                  const course_map_layout& layout,
                                  const render_static_mesh* mesh);
