#pragma once

// A course's rough baked once on a world-fixed half-metre grid: height,
// distance to bare ground and grass kind. The interpolation error bounds
// every edge test, independently of the eye and the density used to draw it.
// Tufts belong to the finest world grid; coarser blocks keep their lowest
// ranked member, with the same root, seed and height. The grid is cut into
// fixed 32 m tiles (grass_tiles.h) baked one at a time and kept packed
// (grass_pack.h, grass_tile_backing.h); their bounds select the blocks to
// draw and the renderer keeps the ones it needs resident. The dense grid
// never exists outside the tests' oracle. GL-free.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
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

// A tile is 64 cells of the global lattice (multiples of grass_field_spacing
// from the world's origin) and the node that closes it on each side, which
// its neighbour shares: bilinear taps never leave their tile.
inline constexpr int grass_tile_cells = 64;
inline constexpr int grass_tile_nodes = grass_tile_cells + 1;
inline constexpr float grass_tile_meters = static_cast<float>(grass_tile_cells) * grass_field_spacing;

// What bounds the grass a tile can hold, over its nodes.
struct grass_tile_info {
    float low = 0.0f;            // lowest node height
    float high = 0.0f;           // highest node height
    float max_clearance = 0.0f;  // greatest node distance to bare ground; 0: nothing can grow
};

class grass_tile_backing;

// Where a field's packed tiles are kept.
enum class grass_storage {
    none,
    memory,        // no cache file could be made: the whole field is held in memory
    file_written,  // baked into a cache file, which holds them
    file_reused,   // an earlier bake's cache file, so no bake at all
};

struct grass_field {
    glm::vec2 origin{0.0f};
    int columns = 0;
    int rows = 0;
    // The tiles covering the nodes, row by row from `first_tile`.
    glm::ivec2 first_tile{0};
    glm::ivec2 tile_counts{0};
    std::vector<grass_tile_info> tiles;
    // Their packed texels, by the same index; shared by copies of the field.
    std::shared_ptr<const grass_tile_backing> backing;
    grass_storage storage = grass_storage::none;
    std::uint64_t revision = 0;
    std::size_t bake_index_bytes = 0;  // the baker's indices at their largest
};

// The meshes contain the exact zone outlines and the paths' actual tops.
// Tee footprints are included directly, without any camera-sized margins.
// With a `cache_directory` (grass_cache.h) the packed tiles go to a file there,
// reused when the same inputs were baked before; without one, or when the
// file cannot be made, they stay in memory (a large course's whole field).
grass_field bake_grass_field(const render_static_mesh& ground, const render_static_mesh& overlay,
                              const std::vector<tee_box>& tees, const std::filesystem::path& cache_directory = {});
// Bytes the field holds in memory: its tile bounds and whatever of the packed tiles is not on disk.
std::uint64_t grass_field_memory_bytes(const grass_field& field);
// Bytes of its cache file, when it has one.
std::uint64_t grass_field_file_bytes(const grass_field& field);
const char* grass_storage_name(grass_storage storage);
// r height, g distance to bare ground (zero on it), b kind (1 rough, 2 heavy),
// at a root as the shader reads it from the stored tile; zero past the field.
// Decodes a tile a call: for checks, not for drawing.
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

// The blocks `frustum` may see, with the tiles their tufts' roots may stand
// in (least to greatest, inclusive). A block reaches from the lowest to the
// highest of the tiles that could grow a blade (`required_clearance`) to
// `top` over it; one over none of them is left out.
struct grass_block_selection {
    std::vector<glm::vec2> corners;  // least x and z of each block
    std::vector<glm::ivec4> tiles;   // x, y least tile; z, w greatest
};
grass_block_selection grass_visible_blocks(const grass_grid& grid, const view_frustum& frustum, const grass_field& field,
                                           float top, float required_clearance, std::size_t* scratch_bound = nullptr);

// The tiles round the eye that the tufts of every ring may stand in (those
// that could grow a blade, with a tile to spare all round), nearest first, at
// most `limit` of them: what the eye may need next, wherever it turns.
std::vector<glm::ivec2> grass_tiles_ahead(const grass_field& field, const glm::vec3& eye, const grass_style& style, std::size_t limit);

// How far across the ground a blade of a ring whose blades are `width_scale`
// times as wide reaches from its root: its lean, wind and all, and half its
// width.
float grass_blade_reach(const grass_style& style, float width_scale);

// One tuft's blades as drawn: per vertex x the side across the blade (-1,
// 1, or 0 at its tip), y the share of its height, z which blade of the tuft.
std::vector<glm::vec3> grass_tuft_vertices(int blades);
