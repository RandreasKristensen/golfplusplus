#pragma once

// The grass tiles on the GPU: three texture arrays of one fixed layer count
// (height R32F, clearance R16 and kind R8_SNORM, texel for texel the same
// layout, see grass_tiles.h) and a table of the layer each world tile holds.
// The tiles a draw misses are uploaded just before it, one layer at a time;
// ahead of the eye a bounded number a frame are fetched too, into layers no
// recent draw used. A tile that leaves has its table entry cleared in the same
// step. Each tile is decoded from the field's backing alone, one at a time, so
// the decoded set in memory is one tile. A tile that cannot be read is not
// resident: its blocks draw no blades, and it is asked for again only after a
// wait. The layer count comes from a byte budget and the driver's limit, never
// from the tuning in use.

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "renderer/gl_storage.h"
#include "renderer/grass_field.h"
#include "renderer/grass_tile_cache.h"
#include "renderer/grass_tiles.h"

class grass_tile_store {
public:
    // Texture units, from `first_unit`, the store binds: height, clearance, kind, table.
    static constexpr int unit_count = 4;

    explicit grass_tile_store(gl_storage storage = {}) : storage_(storage) {}
    // `layer_limit` caps the layers below what the budget and driver allow.
    bool init(int first_unit, int layer_limit = 1 << 30);
    void shutdown();
    int capacity() const { return cache_.capacity(); }
    std::uint64_t logical_bytes() const;
    std::uint64_t tiles_uploaded() const { return tiles_uploaded_; }
    // How many fields the store has been readied for.
    std::uint64_t fields_used() const { return fields_used_; }
    int layer_of(const glm::ivec2& tile) const { return cache_.layer_of(tile); }
    // The textures, for checks that read them back.
    unsigned int height_texture() const { return height_; }
    unsigned int clearance_texture() const { return clearance_; }
    unsigned int kind_texture() const { return kind_; }
    unsigned int table_texture() const { return table_; }
    std::uint64_t tiles_evicted() const { return cache_.evictions(); }
    std::uint64_t tiles_fetched_ahead() const { return tiles_ahead_; }
    std::uint64_t tiles_unreadable() const { return tiles_unreadable_; }
    // Bytes of the decoded tile the uploads go through, and of its packed source.
    std::uint64_t decoded_bytes() const;

    // Readies the store for `field`; its tiles are forgotten when it is another course.
    bool use(const grass_field& field);
    // Makes `tiles` (distinct, at most capacity) resident and binds the textures
    // to its units. The table names every resident tile, or with
    // `only_these` just them: a draw in batches must not draw a tuft twice.
    void make_resident(const grass_field& field, const std::vector<glm::ivec2>& tiles, bool only_these);
    // Fetches up to `budget` of `ahead` (the nearest first) that are not resident,
    // without evicting anything a recent draw used.
    void fill_ahead(const grass_field& field, const std::vector<glm::ivec2>& ahead, int budget);
    // x, y: the table's first tile; z, w: its size.
    glm::ivec4 table_window() const { return glm::ivec4(first_tile_, table_size_); }

private:
    struct identity {
        std::uint64_t revision = 0;
        glm::vec2 origin{0.0f};
        int columns = 0;
        int rows = 0;
        bool operator==(const identity& other) const {
            return revision == other.revision && origin == other.origin && columns == other.columns && rows == other.rows;
        }
    };
    bool upload_tile(const grass_field& field, const glm::ivec2& tile, int layer);
    void apply(const grass_field& field, const grass_tile_cache::change& change);
    static std::uint64_t key_of(const glm::ivec2& tile);
    void write_table(const std::vector<glm::ivec2>& only, bool only_these);

    gl_storage storage_;
    unsigned int height_ = 0;
    unsigned int clearance_ = 0;
    unsigned int kind_ = 0;
    unsigned int table_ = 0;
    grass_tile_cache cache_;
    std::vector<glm::ivec2> layer_tile_;
    std::vector<std::uint16_t> table_data_;
    grass_tile_texels scratch_;
    identity field_;
    bool have_field_ = false;
    bool usable_ = false;
    bool table_stale_ = true;
    bool table_only_ = false;
    glm::ivec2 first_tile_{0};
    glm::ivec2 table_size_{1};
    int first_unit_ = 0;
    int max_texture_size_ = 0;
    std::uint64_t tiles_uploaded_ = 0;
    std::uint64_t tiles_ahead_ = 0;
    std::uint64_t tiles_unreadable_ = 0;
    std::uint64_t draws_ = 0;
    std::uint64_t fields_used_ = 0;
    std::unordered_map<std::uint64_t, std::uint64_t> refused_;  // tile -> the draw it may be asked for again
};
