#include "renderer/grass_tile_cache.h"

#include <algorithm>

#include "renderer/grass_field.h"

int grass_tile_layers(const int limit) {
    const std::uint64_t per_layer = static_cast<std::uint64_t>(grass_tile_nodes) * grass_tile_nodes * grass_tile_texel_bytes;
    return static_cast<int>(std::min<std::uint64_t>(grass_tile_budget_bytes / per_layer, static_cast<std::uint64_t>(std::max(limit, 0))));
}

std::uint64_t grass_tile_cache::key_of(const glm::ivec2& tile) {
    return static_cast<std::uint64_t>(static_cast<std::uint32_t>(tile.x)) << 32 | static_cast<std::uint32_t>(tile.y);
}

void grass_tile_cache::reset(const int capacity) {
    capacity_ = capacity < 0 ? 0 : capacity;
    layers_.clear();
    free_.clear();
    for (int layer = capacity_ - 1; layer >= 0; --layer) { free_.push_back(layer); }
}

int grass_tile_cache::layer_of(const glm::ivec2& tile) const {
    const auto found = layers_.find(key_of(tile));
    return found == layers_.end() ? -1 : found->second.layer;
}

int grass_tile_cache::take_oldest(const std::uint64_t before, std::vector<glm::ivec2>& evicted) {
    auto oldest = layers_.end();
    for (auto at = layers_.begin(); at != layers_.end(); ++at) {
        if (at->second.stamp < before && (oldest == layers_.end() || at->second.stamp < oldest->second.stamp)) { oldest = at; }
    }
    if (oldest == layers_.end()) { return -1; }
    const int layer = oldest->second.layer;
    evicted.emplace_back(static_cast<std::int32_t>(oldest->first >> 32), static_cast<std::int32_t>(oldest->first & 0xffffffffu));
    layers_.erase(oldest);
    ++evictions_;
    return layer;
}

grass_tile_cache::change grass_tile_cache::make_resident(const std::vector<glm::ivec2>& needed) {
    change result;
    ++clock_;
    std::vector<glm::ivec2> missing;
    for (const glm::ivec2& tile : needed) {
        const auto found = layers_.find(key_of(tile));
        if (found != layers_.end()) { found->second.stamp = clock_; }
        else { missing.push_back(tile); }
    }
    for (const glm::ivec2& tile : missing) {
        int layer = -1;
        if (!free_.empty()) {
            layer = free_.back();
            free_.pop_back();
        } else {
            layer = take_oldest(clock_, result.evicted);
            if (layer < 0) { continue; }
        }
        layers_[key_of(tile)] = entry{layer, clock_};
        result.uploads.push_back({tile, layer});
    }
    return result;
}

grass_tile_cache::change grass_tile_cache::fill_ahead(const std::vector<glm::ivec2>& ahead, int budget) {
    change result;
    const std::uint64_t before = clock_ >= grass_tile_rotation_guard ? clock_ - grass_tile_rotation_guard + 1 : 0;
    for (const glm::ivec2& tile : ahead) {
        const auto found = layers_.find(key_of(tile));
        if (found != layers_.end()) {
            found->second.stamp = std::max(found->second.stamp, clock_);
            continue;
        }
        if (budget <= 0) { continue; }
        int layer = -1;
        if (!free_.empty()) {
            layer = free_.back();
            free_.pop_back();
        } else {
            layer = take_oldest(before, result.evicted);
            if (layer < 0) { break; }
        }
        --budget;
        layers_[key_of(tile)] = entry{layer, clock_};
        result.uploads.push_back({tile, layer});
    }
    return result;
}

void grass_tile_cache::forget(const glm::ivec2& tile) {
    const auto found = layers_.find(key_of(tile));
    if (found == layers_.end()) { return; }
    free_.push_back(found->second.layer);
    layers_.erase(found);
}
