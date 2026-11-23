#pragma once

// The grass field's fixed 32 m tiles on its one global half-metre lattice, in
// the three precisions the GPU stores them in, split by meaning: the ground
// height as a float, the clearance (distance to bare ground) as 16-bit
// normalized and rounded down, and the kind exactly. A tile is the unit of
// residency; its texels come from the field alone, so which tiles are
// resident never changes a root, a height or whether a blade grows. GL-free.

#include <cstdint>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "renderer/grass_field.h"

// The tile of a world position.
glm::ivec2 grass_tile_of(const glm::vec2& world);

// The lattice of nodes a field covers: `columns` by `rows` of them, a half
// metre apart from `origin`, which is a whole number of half metres from the
// world's origin. Columns are zero when there is no ground.
struct grass_lattice {
    glm::vec2 origin{0.0f};
    int columns = 0;
    int rows = 0;
    bool operator==(const grass_lattice& other) const {
        return origin == other.origin && columns == other.columns && rows == other.rows;
    }
};

// The tiles over a lattice of `columns` by `rows` nodes from `origin`.
struct grass_tile_extent {
    glm::ivec2 first_cell{0};  // lattice cell of the lattice's first node
    glm::ivec2 first_tile{0};
    glm::ivec2 counts{0};      // zero when the lattice has no cell
};
grass_tile_extent grass_tile_extent_of(const glm::vec2& origin, int columns, int rows);
const grass_tile_info* grass_tile_at(const grass_field& field, const glm::ivec2& tile);
// The tile's index in field.tiles and in its backing, or -1 past the field.
int grass_tile_slot(const grass_field& field, const glm::ivec2& tile);

// One tile's texels, row by row from its least x and z node.
struct grass_tile_texels {
    std::vector<float> height;
    std::vector<std::uint16_t> clearance;  // unorm16 of the distance over grass_field_distance_cap
    std::vector<std::int8_t> kind;         // snorm8 of the kind less one: -1 none, 0 rough, 1 heavy
};

// Nodes past the field repeat its edge's height and kind, with no clearance;
// a tile nothing can grow in is all zero clearance. False when the stored
// tile cannot be read, with `out` the same all-zero tile.
bool fill_grass_tile(const grass_field& field, const glm::ivec2& tile, grass_tile_texels& out);

// Never above the distance given: no blade is accepted that the exact field
// would refuse. The step is grass_field_distance_cap / 65535.
std::uint16_t encode_grass_clearance(float metres);
float decode_grass_clearance(std::uint16_t stored);
std::int8_t encode_grass_kind(float kind);
float decode_grass_kind(std::int8_t stored);

// What grass_blade.vert reads for a root, with its arithmetic: bilinear taps
// within the tile, clearance in metres, kind as the field's b. Zero outside it.
glm::vec3 sample_grass_tile(const grass_tile_texels& texels, const glm::ivec2& tile, const glm::vec2& root);
