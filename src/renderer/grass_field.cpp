#include "renderer/grass_field.h"
#include <algorithm>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

grass_grid grass_grid_around(const glm::vec3& eye, const float reach, const float spacing) {
    grass_grid grid;
    grid.spacing = spacing;
    const int blocks = static_cast<int>(std::ceil((2.0f * reach / spacing + grass_block_tufts) / static_cast<float>(grass_block_tufts)));
    grid.per_row = blocks * grass_block_tufts;
    grid.origin = glm::vec2(std::floor((eye.x - reach) / (spacing * grass_block_tufts)) * grass_block_tufts, std::floor((eye.z - reach) / (spacing * grass_block_tufts)) * grass_block_tufts) * spacing;
    return grid;
}

std::vector<glm::vec2> grass_visible_blocks(const grass_grid& grid, const view_frustum& frustum,
                                            const std::vector<const render_static_mesh*>& ground, const float top) {
    std::vector<render_mesh_bounds> pieces;
    for (const render_static_mesh* mesh : ground) {
        if (mesh == nullptr) {
            continue;
        }
        if (mesh->chunks.empty()) {
            pieces.push_back(mesh->bounds);
        }
        for (const render_mesh_chunk& chunk : mesh->chunks) {
            pieces.push_back(chunk.bounds);
        }
    }
    std::vector<glm::vec2> visible;
    const float side = grid.spacing * static_cast<float>(grass_block_tufts);
    const int blocks = grid.per_row / grass_block_tufts;
    for (int row = 0; row < blocks; ++row) {
        for (int column = 0; column < blocks; ++column) {
            const glm::vec2 corner = grid.origin + glm::vec2(static_cast<float>(column), static_cast<float>(row)) * side;
            float low = 0.0f;
            float high = 0.0f;
            bool grounded = false;
            for (const render_mesh_bounds& piece : pieces) {
                if (!piece.valid || piece.max.x < corner.x || piece.min.x > corner.x + side || piece.max.z < corner.y ||
                    piece.min.z > corner.y + side) {
                    continue;
                }
                low = grounded ? std::min(low, piece.min.y) : piece.min.y;
                high = grounded ? std::max(high, piece.max.y) : piece.max.y;
                grounded = true;
            }
            // A tuft stands anywhere in its cell, the last cell's reaching a
            // spacing past the block.
            if (grounded && frustum_intersects_aabb(frustum, glm::vec3(corner.x, low, corner.y),
                                                    glm::vec3(corner.x + side + grid.spacing, high + top,
                                                              corner.y + side + grid.spacing))) {
                visible.push_back(corner);
            }
        }
    }
    return visible;
}

std::vector<glm::vec3> grass_tuft_vertices(const int blades) {
    std::vector<glm::vec3> vertices;
    for (int blade = 0; blade < blades; ++blade) {
        const float b = static_cast<float>(blade);
        const glm::vec3 root_left(-1.0f, 0.0f, b);
        const glm::vec3 root_right(1.0f, 0.0f, b);
        const glm::vec3 mid_left(-1.0f, 0.5f, b);
        const glm::vec3 mid_right(1.0f, 0.5f, b);
        const glm::vec3 tip(0.0f, 1.0f, b);
        for (const glm::vec3& v : {root_left, root_right, mid_left, root_right, mid_right, mid_left, mid_left, mid_right, tip}) {
            vertices.push_back(v);
        }
    }
    return vertices;
}

float grass_blade_reach(const grass_style& style, const float width_scale) {
    const float tallest = style.blade_height_meters * std::max(1.0f, style.heavy_blade_share) * grass_blade_tallest_share;
    return grass_blade_lean_share * tallest + 0.5f * style.blade_width_meters * width_scale;
}

namespace {
std::uint32_t coarse_hash(const int x, const int z) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8da6b343u ^ static_cast<std::uint32_t>(z) * 0xd8163841u;
    h = (h ^ (h >> 13)) * 0x5bd1e995u;
    return h ^ (h >> 15);
}
}

std::uint32_t grass_cell_rank(const int x, const int z) {
    const auto h = coarse_hash(static_cast<int>(std::floor(x / 8.0)), static_cast<int>(std::floor(z / 8.0)));
    // A permutation of each eight-cell block, with a unique minimum in
    // every aligned power-of-two sub-block. Its minimum is found directly.
    return (h & ~63u) | ((static_cast<std::uint32_t>(x) ^ h) & 7u) |
        (((static_cast<std::uint32_t>(z) ^ (h >> 3)) & 7u) << 3);
}

glm::ivec2 grass_representative(const glm::ivec2& block, const int level) {
    const int side = 1 << std::clamp(level, 0, grass_max_rings - 1);
    const glm::ivec2 origin = block * side;
    const auto h = coarse_hash(static_cast<int>(std::floor(origin.x / 8.0)), static_cast<int>(std::floor(origin.y / 8.0)));
    const auto mask = static_cast<std::uint32_t>(side - 1);
    return origin + glm::ivec2(h & mask, (h >> 3) & mask);
}
