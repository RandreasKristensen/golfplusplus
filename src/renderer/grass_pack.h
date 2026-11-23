#pragma once

// A grass tile in the form it is kept in: the exact nodes the baker made
// (grass_exact_tile) reduced to five bytes a node or fewer. Clearance and
// kind are the GPU's own encodings (grass_tiles.h). Height is a whole number
// of grass_height_step on the one global step, stored as an offset from a
// per-tile integer base, so a node two tiles share decodes the same in both;
// a tile whose relief does not fit 16 bits keeps float heights instead.
// A tile that no accepted blade can grow in is not kept at all. GL-free.

#include <cstdint>
#include <vector>

#include "renderer/grass_field.h"
#include "renderer/grass_tiles.h"

// Every decoded height is within this of the baked one.
inline constexpr float grass_height_step = 0.001f;
inline constexpr float grass_no_ground_height = -1000.0f;

// A tile's 65 x 65 nodes, row by row from its least x and z, at full precision.
struct grass_exact_tile {
    std::vector<float> height;     // grass_no_ground_height where no ground triangle reached
    std::vector<float> clearance;  // metres to bare ground; 0 on it and past the field
    std::vector<float> kind;       // 0 no ground, 1 rough, 2 heavy, between where they meet
};

// Empty when no blade accepted by any style can grow in the tile; `info` is
// then the all-zero one. Otherwise the stored bytes, with the bounds of the
// heights and clearance as decoded.
std::vector<std::uint8_t> pack_grass_tile(const grass_exact_tile& nodes, grass_tile_info& info);
// False when `bytes` is not a tile this build wrote.
bool unpack_grass_tile(const std::uint8_t* bytes, std::size_t size, grass_tile_texels& out);
