#pragma once

// A course's rough baked once on a world-fixed half-metre grid: height,
// distance to bare ground and grass kind. The interpolation error bounds
// every edge test, independently of the eye and the density used to draw it.
// Tufts belong to the finest world grid; coarser blocks keep their lowest
// ranked member, with the same root, seed and height. GL-free.

#include <cstdint>
#include <vector>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include "game/lighting_loader.h"
#include "game/tee_box.h"
#include "renderer/frustum.h"
#include "renderer/render_mesh.h"

inline constexpr float grass_field_spacing = 0.5f;
// A bilinear interpolation of a 1-Lipschitz distance has error at most
// half the cell diagonal. Round upward for floating point arithmetic.
inline constexpr float grass_field_error = 0.354f;
// Beyond the largest blade reach accepted by lighting_fields.h (24.6 m).
inline constexpr float grass_field_distance_cap = 32.0f;
inline constexpr float grass_blade_lean_share = 0.85f;
inline constexpr float grass_blade_tallest_share = 1.4f;
inline constexpr int grass_max_rings = 4;
inline constexpr int grass_block_tufts = 24;

struct grass_field {
    glm::vec2 origin{0.0f};
    int columns = 0;
    int rows = 0;
    // r height, g distance (zero on bare ground), b kind (1 rough, 2 heavy).
    std::vector<glm::vec3> samples;
    std::uint64_t revision = 0;
};

// The meshes contain the exact zone outlines and the paths' actual tops.
// Tee footprints are included directly, without any camera-sized margins.
grass_field bake_grass_field(const render_static_mesh& ground, const render_static_mesh& overlay,
                              const std::vector<tee_box>& tees);
glm::vec3 sample_grass_field(const grass_field& field, const glm::vec2& root);
// Hash and representative match grass_blade.vert, including negative cells.
std::uint32_t grass_cell_rank(int x, int z);
glm::ivec2 grass_representative(const glm::ivec2& block, int level);

// The tufts round the eye: `per_row` by `per_row` of them, `spacing` apart,
// from `origin`, a whole number of spacings from the world's origin, and
// `per_row` a whole number of blocks.
struct grass_grid {
    glm::vec2 origin{0.0f};
    float spacing = 0.0f;
    int per_row = 0;
};

grass_grid grass_grid_around(const glm::vec3& eye, float reach, float spacing);

// The corners (least x and z) of `grid`'s blocks `frustum` may see. A
// block reaches from the lowest of the `ground` under it to `top` over its
// highest; a block over none of it has no ground and is left out.
std::vector<glm::vec2> grass_visible_blocks(const grass_grid& grid, const view_frustum& frustum,
                                            const std::vector<const render_static_mesh*>& ground, float top);

// How far across the ground a blade of a ring whose blades are `width_scale`
// times as wide reaches from its root: its lean, wind and all, and half its
// width.
float grass_blade_reach(const grass_style& style, float width_scale);

// One tuft's blades as drawn: per vertex x the side across the blade (-1,
// 1, or 0 at its tip), y the share of its height, z which blade of the tuft.
std::vector<glm::vec3> grass_tuft_vertices(int blades);
