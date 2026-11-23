#include "renderer/grass_field.h"

#include <functional>

#include "renderer/grass_baker.h"
#include "renderer/grass_cache.h"
#include "renderer/grass_tile_backing.h"
#include "renderer/grass_tiles.h"

namespace {
// Bakes every tile in turn and hands each one's packed bytes and bounds to
// `keep`, which says whether to go on.
void bake_tiles(grass_baker& baker, const grass_tile_extent& extent, std::vector<grass_tile_info>& tiles,
                const std::function<bool(std::size_t, const std::vector<std::uint8_t>&, const grass_tile_info&)>& keep) {
    grass_exact_tile exact;
    for (int tz = 0; tz < extent.counts.y; ++tz) {
        for (int tx = 0; tx < extent.counts.x; ++tx) {
            const std::size_t slot = static_cast<std::size_t>(tz) * extent.counts.x + tx;
            if (!baker.bake(extent.first_tile + glm::ivec2(tx, tz), exact)) { continue; }
            const std::vector<std::uint8_t> bytes = pack_grass_tile(exact, tiles[slot]);
            if (!keep(slot, bytes, tiles[slot])) { return; }
        }
    }
}
}

grass_field bake_grass_field(const render_static_mesh& ground, const render_static_mesh& overlay,
                             const std::vector<tee_box>& tees, const std::filesystem::path& cache_directory) {
    grass_field field;
    field.revision = ground.revision;
    const grass_lattice lattice = grass_lattice_of(ground);
    if (lattice.columns < 2) { return field; }
    field.origin = lattice.origin;
    field.columns = lattice.columns;
    field.rows = lattice.rows;
    const grass_tile_extent extent = grass_tile_extent_of(field.origin, field.columns, field.rows);
    field.first_tile = extent.first_tile;
    field.tile_counts = extent.counts;
    const std::size_t count = static_cast<std::size_t>(extent.counts.x) * extent.counts.y;
    field.tiles.assign(count, grass_tile_info{});

    grass_cache_shape shape;
    if (!cache_directory.empty()) {
        shape = {grass_content_key(ground, overlay, tees), lattice, extent};
        if (auto cached = open_grass_cache(cache_directory, shape, field.tiles)) {
            field.backing = std::move(cached);
            field.storage = grass_storage::file_reused;
            return field;
        }
    }

    grass_baker baker(ground, overlay, tees);
    if (!cache_directory.empty()) {
        grass_cache_writer writer(cache_directory, shape);
        if (writer.ok()) {
            bake_tiles(baker, extent, field.tiles, [&](const std::size_t slot, const std::vector<std::uint8_t>& bytes, const grass_tile_info& info) {
                writer.put(slot, bytes, info);
                return writer.ok();
            });
            field.bake_index_bytes = baker.index_bytes();
            if (auto written = writer.finish()) {
                field.backing = std::move(written);
                field.storage = grass_storage::file_written;
                return field;
            }
        }
        // The file could not be made or finished: the tiles stay in memory (grass_field.h).
        field.tiles.assign(count, grass_tile_info{});
    }
    auto backing = std::make_shared<grass_memory_backing>(count);
    bake_tiles(baker, extent, field.tiles, [&](const std::size_t slot, const std::vector<std::uint8_t>& bytes, const grass_tile_info&) {
        backing->put(slot, bytes);
        return true;
    });
    field.bake_index_bytes = baker.index_bytes();
    field.backing = std::move(backing);
    field.storage = grass_storage::memory;
    return field;
}
