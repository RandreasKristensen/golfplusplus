#pragma once

// Where the rough's blades stand round the eye (renderer/grass_renderer.h).
// GL-free. They stand in rings, each twice as far out as the one inside it
// with its tufts twice as far apart and twice as wide, so the rough looks
// grassy as far as the eye can tell. For each ring the ground round the eye
// is mapped from above into a square of texels (its height and how grassy it
// is), mapped again only once the eye has gone a quarter of the way to its
// edge; the tufts stand on a grid fixed to the world, so they never swim as
// the eye moves, and are drawn in square blocks, only those the eye may see.

#include <vector>

#include "game/tee_box.h"
#include "renderer/frustum.h"
#include "renderer/render_mesh.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

inline constexpr int grass_map_texels = 256;
inline constexpr int grass_max_rings = 4;
// A block's tufts each way.
inline constexpr int grass_block_tufts = 24;
// How far above and below the window's base height the map reaches.
inline constexpr float grass_map_depth_meters = 200.0f;

// The square mapped from above: its corner of least x and z, its side, and
// the height its texels' heights are kept relative to.
struct grass_window {
    glm::vec2 min{0.0f};
    float size = 0.0f;
    float base = 0.0f;
};

// The window for an eye at `eye` whose blades reach `reach` metres: centred
// on it, a margin wider each way, its corner on a whole texel.
grass_window grass_window_around(const glm::vec3& eye, float reach);

// Whether `window` still covers the blades of an eye at `eye`.
bool grass_window_serves(const grass_window& window, const glm::vec3& eye, float reach);

// Looking straight down on `window`: world x to the map's x, z to its y, and
// higher ground nearer, so the top surface is what is mapped.
glm::mat4 grass_window_view_proj(const grass_window& window);

// The tufts round the eye: `per_row` by `per_row` of them, `spacing` apart,
// from `origin`, a whole number of spacings from the world's origin, and
// `per_row` a whole number of blocks.
struct grass_grid {
    glm::vec2 origin{0.0f};
    float spacing = 0.0f;
    int per_row = 0;
};

grass_grid grass_grid_around(const glm::vec3& eye, float reach, float spacing);

// The corners (least x and z) of `grid`'s blocks `frustum` may see, but
// for those wholly within `hollow` metres of `eye` across the ground. A
// block reaches from the lowest of the `ground` under it to `top` over its
// highest; a block over none of it has no ground and is left out.
std::vector<glm::vec2> grass_visible_blocks(const grass_grid& grid, const view_frustum& frustum,
                                            const std::vector<const render_static_mesh*>& ground, float top,
                                            const glm::vec3& eye, float hollow);

// The tops of `boxes`, a little wider than they are, as bare ground for the
// map: two triangles each.
std::vector<render_terrain_vertex> grass_bare_tee_tops(const std::vector<tee_box>& boxes);

// One tuft's blades as drawn: per vertex x the side across the blade (-1,
// 1, or 0 at its tip), y the share of its height, z which blade of the tuft.
std::vector<glm::vec3> grass_tuft_vertices(int blades);
