#pragma once

// What the picture checks share: a course loaded as the game loads it, with
// its render meshes, read back pictures and pictures written for a person to
// look at when a check fails.

#include "game/game_state.h"
#include "game/lighting_loader.h"
#include "renderer/framebuffer.h"
#include "renderer/render_mesh.h"
#include "renderer/grass_field.h"
#include "renderer/grass_tiles.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/vec4.hpp>

// A course started as GOLFPP_COURSE starts it, and its ground as the
// renderer draws it (core/app_scene.cpp).
struct check_course {
    game_state game;
    render_static_mesh terrain;
    render_static_mesh overlay;
    grass_field grass;
};

check_course load_check_course(const game_content& content, const course_definition& course, const lighting_palette& palette);

// Calls `visit(position, node)` for every `stride`th node of the lattice in
// row order (index row * columns + column), node being height, clearance and
// kind as stored (a tile nothing grows in reads as no clearance); stops and
// returns true when it answers true. Tiles are decoded a tile row at a time.
template <typename visit_node>
bool for_each_grass_node(const grass_field& field, const std::size_t stride, visit_node&& visit) {
    const glm::ivec2 first_cell = glm::ivec2(glm::round(field.origin / grass_field_spacing));
    std::vector<grass_tile_texels> row_tiles(static_cast<std::size_t>(std::max(field.tile_counts.x, 0)));
    int loaded_row = std::numeric_limits<int>::min();
    for (int z = 0; z < field.rows; ++z) {
        for (int x = 0; x < field.columns; ++x) {
            if ((static_cast<std::size_t>(z) * static_cast<std::size_t>(field.columns) + static_cast<std::size_t>(x)) % stride != 0) { continue; }
            const glm::ivec2 cell = first_cell + glm::ivec2(x, z);
            const glm::ivec2 tile(static_cast<int>(std::floor(static_cast<double>(cell.x) / grass_tile_cells)),
                                  static_cast<int>(std::floor(static_cast<double>(cell.y) / grass_tile_cells)));
            if (tile.y != loaded_row) {
                for (int tx = 0; tx < field.tile_counts.x; ++tx) { fill_grass_tile(field, glm::ivec2(field.first_tile.x + tx, tile.y), row_tiles[static_cast<std::size_t>(tx)]); }
                loaded_row = tile.y;
            }
            const grass_tile_texels& texels = row_tiles[static_cast<std::size_t>(tile.x - field.first_tile.x)];
            const glm::ivec2 local = cell - tile * grass_tile_cells;
            const std::size_t i = static_cast<std::size_t>(local.y) * grass_tile_nodes + local.x;
            const glm::vec2 at = field.origin + glm::vec2(x, z) * grass_field_spacing;
            if (visit(at, glm::vec3(texels.height[i], decode_grass_clearance(texels.clearance[i]), decode_grass_kind(texels.kind[i])))) { return true; }
        }
    }
    return false;
}

std::string check_shader_path(const std::string& asset_root, const char* name);

// The framebuffer's colour in its corner `width` by `height`, row by row
// from the bottom.
std::vector<glm::vec4> read_colors(const framebuffer& target, int width, int height);

// A 24-bit BMP of `pixels` (row by row from the bottom). False when it
// cannot be written.
bool write_check_bmp(const std::filesystem::path& path, int width, int height,
                     const std::vector<std::array<std::uint8_t, 3>>& pixels);
