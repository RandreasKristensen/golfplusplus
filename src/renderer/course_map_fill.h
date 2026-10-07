#pragma once

// GL-free builder for the paper course map's terrain fill.
//
// The fill scan-converts every terrain triangle into map-space strip quads
// (thousands of quads for a course). It depends only on the terrain mesh and
// the map layout, so the vertices are cached and rebuilt only when one of
// those changes; while the map is open the layout normally stays fixed
// because the course's holes dominate it (course_map_overlay.h).
//
// Kept free of GL and render_data so the vertices can be unit tested against
// the uncached builder.
//
// It is also the one home of how a printed map looks: the inks below are
// shared with every other map of the course (the hole signs' faces).

#include <cstddef>
#include <cstdint>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "physics/terrain.h"
#include "renderer/overlay_batch.h"
#include "renderer/render_mesh.h"

// The paper, and the ink of what is marked on it.
inline const glm::vec3 course_map_paper(0.76f, 0.72f, 0.55f);
inline const glm::vec3 course_map_tree_ink(0.08f, 0.24f, 0.11f);
inline const glm::vec3 course_map_tee_ink(0.34f, 0.21f, 0.12f);
inline const glm::vec3 course_map_flagstick_ink(0.06f, 0.04f, 0.025f);
inline const glm::vec3 course_map_flag_ink(0.76f, 0.17f, 0.12f);

// The ink a patch of ground of this colour (terrain_palette.h) is printed in.
glm::vec3 course_map_ink(glm::vec3 ground_color);
// The ink of a surface: every map prints fairway, rough, green, bunker and
// water this way, so they agree with each other and with the 3D ground.
glm::vec3 course_map_material_ink(terrain_material material);

struct course_map_layout {
    glm::vec2 center{0.0f, 0.02f};
    glm::vec2 half_size{0.68f, 0.76f};
    glm::vec3 world_center{0.0f};
    // Clip units per world unit along x and y: equal on screen, so different
    // in clip units unless the target is square.
    glm::vec2 scale{0.01f};
};

// True when both layouts produce exactly the same map-space geometry.
bool course_map_layouts_match(const course_map_layout& a, const course_map_layout& b);

// World position -> overlay clip space. The paper map reads from the player's
// perspective, so world +X maps left.
glm::vec2 map_point(const course_map_layout& layout, const glm::vec3& position);

// Appends the clipped scan-converted strip quads of one map-space triangle;
// one thinner than a strip still gets one, so small triangles (the ground's
// fine cells around bunkers and ponds) are never dropped.
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
