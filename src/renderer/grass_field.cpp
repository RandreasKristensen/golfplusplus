#include "renderer/grass_field.h"
#include "renderer/grass_tiles.h"
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

grass_block_selection grass_visible_blocks(const grass_grid& grid, const view_frustum& frustum, const grass_field& field,
                                           const float top, const float required_clearance, std::size_t* scratch_bound) {
    grass_block_selection visible;
    const float side = grid.spacing * static_cast<float>(grass_block_tufts);
    const int blocks = grid.per_row / grass_block_tufts;
    for (int row = 0; row < blocks; ++row) {
        for (int column = 0; column < blocks; ++column) {
            const glm::vec2 corner = grid.origin + glm::vec2(static_cast<float>(column), static_cast<float>(row)) * side;
            // A tuft stands anywhere in its cell, the last cell's reaching a
            // spacing past the block.
            const glm::vec2 outer = corner + glm::vec2(side + grid.spacing);
            const glm::ivec2 first = glm::max(grass_tile_of(corner), field.first_tile);
            const glm::ivec2 last = glm::min(grass_tile_of(outer), field.first_tile + field.tile_counts - 1);
            float low = 0.0f;
            float high = 0.0f;
            bool grounded = false;
            for (int z = first.y; z <= last.y; ++z) {
                for (int x = first.x; x <= last.x; ++x) {
                    const grass_tile_info* tile = grass_tile_at(field, glm::ivec2(x, z));
                    if (tile == nullptr || tile->max_clearance < required_clearance) { continue; }
                    low = grounded ? std::min(low, tile->low) : tile->low;
                    high = grounded ? std::max(high, tile->high) : tile->high;
                    grounded = true;
                }
            }
            const glm::vec3 box_min(corner.x, low, corner.y);
            const glm::vec3 box_max(outer.x, high + top, outer.y);
            if (grounded && frustum_intersects_aabb(frustum, box_min, box_max) && !frustum_hides_aabb(frustum, box_min, box_max)) {
                visible.corners.push_back(corner);
                visible.tiles.emplace_back(first, last);
            }
        }
    }
    if (scratch_bound) {
        const auto bytes = visible.corners.capacity() * sizeof(glm::vec2) + visible.tiles.capacity() * sizeof(glm::ivec4);
        *scratch_bound = std::max(*scratch_bound, bytes);
    }
    return visible;
}

std::vector<glm::ivec2> grass_tiles_ahead(const grass_field& field, const glm::vec3& eye, const grass_style& style, const std::size_t limit) {
    std::vector<glm::ivec2> tiles;
    if (field.tiles.empty() || limit == 0 || style.blade_height_meters <= 0.0f) { return tiles; }
    const int ring_count = std::clamp(static_cast<int>(std::lround(style.rings)), 1, grass_max_rings);
    const float scale = std::ldexp(1.0f, ring_count - 1);
    const float required = grass_blade_reach(style, scale) + grass_field_error;
    // The outermost ring's grid holds the others', a tile of margin covers their block alignment.
    const grass_grid grid = grass_grid_around(eye, style.reach_meters * scale, style.tuft_spacing_meters * scale);
    const glm::vec2 end = grid.origin + glm::vec2(static_cast<float>(grid.per_row) * grid.spacing + grid.spacing);
    const glm::ivec2 first = glm::max(grass_tile_of(grid.origin) - 1, field.first_tile);
    const glm::ivec2 last = glm::min(grass_tile_of(end) + 1, field.first_tile + field.tile_counts - 1);
    const glm::vec2 here(eye.x, eye.z);
    for (int z = first.y; z <= last.y; ++z) {
        for (int x = first.x; x <= last.x; ++x) {
            const grass_tile_info* info = grass_tile_at(field, glm::ivec2(x, z));
            if (info != nullptr && info->max_clearance >= required) { tiles.emplace_back(x, z); }
        }
    }
    const auto distance = [&](const glm::ivec2& tile) {
        const glm::vec2 delta = (glm::vec2(tile) + 0.5f) * grass_tile_meters - here;
        return glm::dot(delta, delta);
    };
    std::sort(tiles.begin(), tiles.end(), [&](const glm::ivec2& a, const glm::ivec2& b) {
        const float da = distance(a);
        const float db = distance(b);
        return da != db ? da < db : (a.y != b.y ? a.y < b.y : a.x < b.x);
    });
    if (tiles.size() > limit) { tiles.resize(limit); }
    return tiles;
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
