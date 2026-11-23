#include "renderer/grass_tiles.h"

#include <algorithm>
#include <cmath>
#include <glm/common.hpp>

#include "renderer/grass_pack.h"
#include "renderer/grass_tile_backing.h"

namespace {
int floor_div(const int a, const int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
}

glm::ivec2 grass_tile_of(const glm::vec2& world) {
    return glm::ivec2(glm::floor(world / grass_tile_meters));
}

grass_tile_extent grass_tile_extent_of(const glm::vec2& origin, const int columns, const int rows) {
    grass_tile_extent extent;
    if (columns < 2 || rows < 2) { return extent; }
    extent.first_cell = glm::ivec2(glm::round(origin / grass_field_spacing));
    const glm::ivec2 last = extent.first_cell + glm::ivec2(columns - 1, rows - 1);
    extent.first_tile = glm::ivec2(floor_div(extent.first_cell.x, grass_tile_cells), floor_div(extent.first_cell.y, grass_tile_cells));
    extent.counts = glm::ivec2(floor_div(last.x, grass_tile_cells), floor_div(last.y, grass_tile_cells)) - extent.first_tile + 1;
    return extent;
}

int grass_tile_slot(const grass_field& field, const glm::ivec2& tile) {
    const glm::ivec2 at = tile - field.first_tile;
    if (at.x < 0 || at.y < 0 || at.x >= field.tile_counts.x || at.y >= field.tile_counts.y) { return -1; }
    return at.y * field.tile_counts.x + at.x;
}

const grass_tile_info* grass_tile_at(const grass_field& field, const glm::ivec2& tile) {
    const int slot = grass_tile_slot(field, tile);
    return slot < 0 || static_cast<std::size_t>(slot) >= field.tiles.size() ? nullptr : &field.tiles[static_cast<std::size_t>(slot)];
}

bool fill_grass_tile(const grass_field& field, const glm::ivec2& tile, grass_tile_texels& out) {
    const grass_tile_info* info = grass_tile_at(field, tile);
    const std::size_t count = static_cast<std::size_t>(grass_tile_nodes) * grass_tile_nodes;
    std::vector<std::uint8_t> bytes;
    const bool stored = info != nullptr && info->max_clearance > 0.0f;
    if (stored && field.backing && field.backing->read(static_cast<std::size_t>(grass_tile_slot(field, tile)), bytes) &&
        unpack_grass_tile(bytes.data(), bytes.size(), out)) {
        return true;
    }
    out.height.assign(count, 0.0f);
    out.clearance.assign(count, 0);
    out.kind.assign(count, encode_grass_kind(0.0f));
    return !stored;
}

std::uint64_t grass_field_memory_bytes(const grass_field& field) {
    return field.tiles.capacity() * sizeof(grass_tile_info) + (field.backing ? field.backing->memory_bytes() : 0);
}

std::uint64_t grass_field_file_bytes(const grass_field& field) { return field.backing ? field.backing->file_bytes() : 0; }

const char* grass_storage_name(const grass_storage storage) {
    switch (storage) {
        case grass_storage::none: return "none";
        case grass_storage::memory: return "memory";
        case grass_storage::file_written: return "file_written";
        case grass_storage::file_reused: return "file_reused";
    }
    return "none";
}

glm::vec3 sample_grass_field(const grass_field& field, const glm::vec2& root) {
    const glm::vec2 end = field.origin + glm::vec2(static_cast<float>(field.columns - 1), static_cast<float>(field.rows - 1)) * grass_field_spacing;
    if (field.columns < 2 || field.rows < 2 || root.x < field.origin.x || root.y < field.origin.y || root.x >= end.x || root.y >= end.y) {
        return glm::vec3(0.0f);
    }
    const glm::ivec2 tile = grass_tile_of(root);
    grass_tile_texels texels;
    fill_grass_tile(field, tile, texels);
    return sample_grass_tile(texels, tile, root);
}

std::uint16_t encode_grass_clearance(const float metres) {
    if (!(metres > 0.0f)) { return 0; }
    if (metres >= grass_field_distance_cap) { return 65535; }
    auto stored = static_cast<long>(std::floor(static_cast<double>(metres) * 65535.0 / grass_field_distance_cap));
    stored = std::clamp(stored, 0L, 65535L);
    while (stored > 0 && static_cast<double>(stored) * grass_field_distance_cap / 65535.0 > static_cast<double>(metres)) { --stored; }
    return static_cast<std::uint16_t>(stored);
}

float decode_grass_clearance(const std::uint16_t stored) {
    return static_cast<float>(stored) / 65535.0f * grass_field_distance_cap;
}

std::int8_t encode_grass_kind(const float kind) {
    return static_cast<std::int8_t>(std::lround(std::clamp(kind - 1.0f, -1.0f, 1.0f) * 127.0f));
}

float decode_grass_kind(const std::int8_t stored) {
    return std::max(static_cast<float>(stored) / 127.0f, -1.0f) + 1.0f;
}

glm::vec3 sample_grass_tile(const grass_tile_texels& texels, const glm::ivec2& tile, const glm::vec2& root) {
    const glm::vec2 at = root / grass_field_spacing;
    const glm::ivec2 cell = glm::ivec2(glm::floor(at));
    const glm::vec2 f = at - glm::vec2(cell);
    const glm::ivec2 local = cell - tile * grass_tile_cells;
    if (local.x < 0 || local.y < 0 || local.x >= grass_tile_cells || local.y >= grass_tile_cells) { return glm::vec3(0.0f); }
    const auto node = [&](const int dx, const int dy) {
        const std::size_t i = static_cast<std::size_t>(local.y + dy) * grass_tile_nodes + local.x + dx;
        return glm::vec3(texels.height[i], decode_grass_clearance(texels.clearance[i]), decode_grass_kind(texels.kind[i]));
    };
    return glm::mix(glm::mix(node(0, 0), node(1, 0), f.x), glm::mix(node(0, 1), node(1, 1), f.x), f.y);
}
