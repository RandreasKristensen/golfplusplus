#pragma once

// Which grass tiles hold which layer of a fixed number of GPU layers: the
// least recently needed leaves when a missing one needs room. A tile fetched
// ahead of need only takes a layer no draw has used for a while (the rotation
// guard), so the driver is never asked to overwrite what a frame still in
// flight may be reading. It only decides; the renderer uploads what it
// reports. GL-free.

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/vec2.hpp>

// GPU bytes of the three arrays at full size, within the section 1 budget.
inline constexpr std::uint64_t grass_tile_budget_bytes = 7ull << 20;
inline constexpr int grass_tile_texel_bytes = 4 + 2 + 1;  // height, clearance, kind

// Layers the budget affords, and `limit` (GL_MAX_ARRAY_TEXTURE_LAYERS) permits.
int grass_tile_layers(int limit);

// A layer a draw used fewer than this many draws ago is not overwritten for a
// tile that is only wanted ahead (three views a frame, a few frames in flight).
inline constexpr std::uint64_t grass_tile_rotation_guard = 12;
// Draws a tile that could not be read waits before it is asked for again.
inline constexpr std::uint64_t grass_tile_retry_draws = 240;

class grass_tile_cache {
public:
    struct upload {
        glm::ivec2 tile{0};
        int layer = 0;
    };
    struct change {
        std::vector<upload> uploads;      // tiles that were missing, with the layer now theirs
        std::vector<glm::ivec2> evicted;  // tiles that lost their layer to them
    };

    explicit grass_tile_cache(int capacity = 0) { reset(capacity); }
    // Empties the cache, as for another course.
    void reset(int capacity);
    int capacity() const { return capacity_; }
    int resident() const { return static_cast<int>(layers_.size()); }
    // The layer of a resident tile, or -1.
    int layer_of(const glm::ivec2& tile) const;
    // Makes every tile of `needed` (no repeats, at most capacity) resident,
    // evicting only tiles not in it, least recently needed first.
    change make_resident(const std::vector<glm::ivec2>& needed);
    // Keeps the resident tiles of `ahead` (in order of want) as recent as a
    // draw would, and makes up to `budget` more of them resident in free
    // layers or those past the guard. Stops at the first it has no layer for.
    change fill_ahead(const std::vector<glm::ivec2>& ahead, int budget);
    // Drops a tile whose texels could not be had; its layer is free again.
    void forget(const glm::ivec2& tile);
    std::uint64_t evictions() const { return evictions_; }

private:
    struct entry {
        int layer = 0;
        std::uint64_t stamp = 0;
    };
    static std::uint64_t key_of(const glm::ivec2& tile);
    // The layer of the resident tile least recently needed before `before`, taken from it; -1 when there is none.
    int take_oldest(std::uint64_t before, std::vector<glm::ivec2>& evicted);
    int capacity_ = 0;
    std::uint64_t clock_ = 0;
    std::uint64_t evictions_ = 0;
    std::unordered_map<std::uint64_t, entry> layers_;
    std::vector<int> free_;
};
