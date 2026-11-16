#include "renderer/grass_field.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
// The window's side over the blades' reach each way.
constexpr float window_margin_share = 0.5f;
// How far past a tee box's edge no grass grows, so no blade leans over it.
constexpr float tee_clear_meters = 0.1f;
}

std::vector<render_terrain_vertex> grass_bare_tee_tops(const std::vector<tee_box>& boxes) {
    std::vector<render_terrain_vertex> vertices;
    for (const tee_box& box : boxes) {
        const glm::vec3 along = box.down_hole * (box.half_length + tee_clear_meters);
        const glm::vec3 across = glm::vec3(-box.down_hole.z, 0.0f, box.down_hole.x) * (box.half_width + tee_clear_meters);
        const glm::vec3 corners[4] = {box.center - along - across, box.center + along - across, box.center + along + across,
                                      box.center - along + across};
        for (const int i : {0, 1, 2, 0, 2, 3}) {
            render_terrain_vertex vertex;
            vertex.position = corners[i];
            vertices.push_back(vertex);
        }
    }
    return vertices;
}

grass_window grass_window_around(const glm::vec3& eye, const float reach) {
    grass_window window;
    window.size = 2.0f * reach * (1.0f + window_margin_share);
    const float texel = window.size / static_cast<float>(grass_map_texels);
    const glm::vec2 corner(eye.x - 0.5f * window.size, eye.z - 0.5f * window.size);
    window.min = glm::vec2(std::floor(corner.x / texel), std::floor(corner.y / texel)) * texel;
    window.base = std::round(eye.y);
    return window;
}

bool grass_window_serves(const grass_window& window, const glm::vec3& eye, const float reach) {
    if (window.size <= 0.0f) {
        return false;
    }
    const glm::vec2 centre = window.min + glm::vec2(0.5f * window.size);
    const float moved = std::max(std::abs(eye.x - centre.x), std::abs(eye.z - centre.y));
    return moved <= 0.5f * reach * window_margin_share && std::abs(eye.y - window.base) < 0.25f * grass_map_depth_meters;
}

glm::mat4 grass_window_view_proj(const grass_window& window) {
    const float half = 0.5f * window.size;
    const glm::vec2 centre = window.min + glm::vec2(half);
    glm::mat4 m(0.0f);
    m[0][0] = 1.0f / half;                     // x to the map's x
    m[2][1] = 1.0f / half;                     // z to the map's y
    m[1][2] = -1.0f / grass_map_depth_meters;  // higher is nearer
    m[3][0] = -centre.x / half;
    m[3][1] = -centre.y / half;
    m[3][2] = window.base / grass_map_depth_meters;
    m[3][3] = 1.0f;
    return m;
}

grass_grid grass_grid_around(const glm::vec3& eye, const float reach, const float spacing) {
    grass_grid grid;
    grid.spacing = spacing;
    const int blocks = static_cast<int>(std::ceil((2.0f * reach / spacing + 1.0f) / static_cast<float>(grass_block_tufts)));
    grid.per_row = blocks * grass_block_tufts;
    grid.origin = glm::vec2(std::floor((eye.x - reach) / spacing), std::floor((eye.z - reach) / spacing)) * spacing;
    return grid;
}

std::vector<glm::vec2> grass_visible_blocks(const grass_grid& grid, const view_frustum& frustum,
                                            const std::vector<const render_static_mesh*>& ground, const float top,
                                            const glm::vec3& eye, const float hollow) {
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
            const glm::vec2 farthest = glm::max(glm::abs(corner - glm::vec2(eye.x, eye.z)),
                                                glm::abs(corner + glm::vec2(side + grid.spacing) - glm::vec2(eye.x, eye.z)));
            if (glm::length(farthest) < hollow) {
                continue;
            }
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
