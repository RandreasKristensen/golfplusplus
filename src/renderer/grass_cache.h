#pragma once

// The disposable file a course's packed grass tiles are kept in, so that a
// 3 km course costs a few megabytes of memory instead of its field's size, and
// so that the next load of the same course skips the bake. One file a course,
// named and keyed by a hash of everything the bake and the packing read: the
// meshes' positions, blades and indices, the tees, every constant and the file
// format's version. A file for other inputs or by an older build is never
// found; a damaged one is removed and baked again. Nothing here is a save:
// deleting the directory costs only the bake. Errors are filesystem error
// codes and return values, never exceptions. GL-free.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include "game/tee_box.h"
#include "renderer/grass_field.h"
#include "renderer/grass_tile_backing.h"
#include "renderer/grass_tiles.h"
#include "renderer/render_mesh.h"

inline constexpr std::uint32_t grass_cache_version = 1;
// Cache files kept in a directory: the newest ones within this many bytes.
inline constexpr std::uint64_t grass_cache_budget_bytes = 1ull << 30;

std::uint64_t grass_content_key(const render_static_mesh& ground, const render_static_mesh& overlay, const std::vector<tee_box>& tees);

// What a cache file describes: the inputs' key, the lattice and its tiles.
struct grass_cache_shape {
    std::uint64_t key = 0;
    grass_lattice lattice;
    grass_tile_extent extent;
};

// The file for `shape` in `directory`. A missing file, one for other
// inputs, or a damaged one gives null (the damaged one is removed first).
// `infos` gets the tile bounds the file holds.
std::shared_ptr<const grass_tile_backing> open_grass_cache(const std::filesystem::path& directory, const grass_cache_shape& shape,
                                                           std::vector<grass_tile_info>& infos);

// Writes a file as tiles are baked, a tile at a time, never holding them.
class grass_cache_writer {
public:
    grass_cache_writer(const std::filesystem::path& directory, const grass_cache_shape& shape);
    ~grass_cache_writer();
    grass_cache_writer(const grass_cache_writer&) = delete;
    grass_cache_writer& operator=(const grass_cache_writer&) = delete;
    // False when the file could not be made or a write failed.
    bool ok() const;
    // Appends slot's packed bytes (none for a tile nothing grows in) and bounds.
    void put(std::size_t slot, const std::vector<std::uint8_t>& bytes, const grass_tile_info& info);
    // Completes the file under its name, removes the older files beyond the
    // directory's budget and opens it for reading; null when any step failed.
    std::shared_ptr<const grass_tile_backing> finish();

private:
    class state;
    std::unique_ptr<state> state_;
};
