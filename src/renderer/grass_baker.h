#pragma once

// Rasterizes the course's ground, rough overlays, bare zones and tees onto the
// global half-metre lattice one 32 m tile at a time, so the dense field is
// never allocated. It indexes the triangles by the tiles their nodes fall in
// (keeping the meshes' order, which decides height ties) and the exact bare
// edges once. A tile's nodes are what one dense raster of the whole lattice
// gives them, bit for bit. Refers to the meshes it was made from. GL-free.

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/vec2.hpp>

#include "game/tee_box.h"
#include "renderer/grass_distance.h"
#include "renderer/grass_pack.h"
#include "renderer/grass_tiles.h"
#include "renderer/render_mesh.h"

// The lattice over the ground's bounds.
grass_lattice grass_lattice_of(const render_static_mesh& ground);

class grass_baker {
public:
    grass_baker(const render_static_mesh& ground, const render_static_mesh& overlay, const std::vector<tee_box>& tees);

    // The lattice: nodes from `origin` a half metre apart; columns < 2 when there is no ground.
    glm::vec2 origin() const { return origin_; }
    int columns() const { return columns_; }
    int rows() const { return rows_; }
    // Bytes of the indices it holds.
    std::size_t index_bytes() const;
    // False when no ground triangle reaches the tile, so nothing grows in it.
    bool bake(const glm::ivec2& tile, grass_exact_tile& out);

private:
    struct triangle_lists {
        std::vector<std::uint32_t> first;  // per tile, plus the end
        std::vector<std::uint32_t> items;
    };
    const render_static_mesh& ground_;
    const render_static_mesh& overlay_;
    std::vector<std::array<glm::vec2, 3>> tees_;
    glm::vec2 origin_{0.0f};
    int columns_ = 0;
    int rows_ = 0;
    glm::ivec2 first_cell_{0};
    glm::ivec2 first_tile_{0};
    glm::ivec2 tile_counts_{0};
    triangle_lists ground_lists_;
    triangle_lists overlay_lists_;
    grass_bare_distance distance_;
    std::vector<glm::vec3> grid_;
};
