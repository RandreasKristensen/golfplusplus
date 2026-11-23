#include "renderer/caster_index.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>

#include <glm/common.hpp>

std::vector<glm::vec3> caster_index::build(const std::vector<glm::vec3>& corners) {
    // (tile z, tile x) orders the tiles along rows.
    std::map<std::pair<int, int>, std::vector<std::size_t>> by_tile;
    const std::size_t triangles = corners.size() / 3;
    for (std::size_t t = 0; t < triangles; ++t) {
        const glm::vec3 centre = (corners[3 * t] + corners[3 * t + 1] + corners[3 * t + 2]) / 3.0f;
        by_tile[{static_cast<int>(std::floor(centre.z / tile_meters)), static_cast<int>(std::floor(centre.x / tile_meters))}]
            .push_back(t);
    }
    tiles_.clear();
    std::vector<glm::vec3> sorted;
    sorted.reserve(triangles * 3);
    for (const auto& [key, members] : by_tile) {
        tile entry;
        entry.first = sorted.size();
        entry.low = glm::vec3(1e30f);
        entry.high = glm::vec3(-1e30f);
        for (const std::size_t t : members) {
            for (std::size_t c = 0; c < 3; ++c) {
                const glm::vec3& corner = corners[3 * t + c];
                entry.low = glm::min(entry.low, corner);
                entry.high = glm::max(entry.high, corner);
                sorted.push_back(corner);
            }
        }
        entry.count = members.size() * 3;
        tiles_.push_back(entry);
    }
    vertex_count_ = sorted.size();
    return sorted;
}

const std::vector<caster_run>& caster_index::select(const glm::vec3& eye, const float reach) {
    runs_.clear();
    for (const tile& entry : tiles_) {
        const float dx = std::max({entry.low.x - eye.x, 0.0f, eye.x - entry.high.x});
        const float dz = std::max({entry.low.z - eye.z, 0.0f, eye.z - entry.high.z});
        if (dx * dx + dz * dz > reach * reach) {
            continue;
        }
        if (!runs_.empty() && runs_.back().first + runs_.back().count == entry.first) {
            runs_.back().count += entry.count;
        } else {
            runs_.push_back(caster_run{entry.first, entry.count});
        }
    }
    return runs_;
}
