#include "game/mover_path.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>

namespace {
constexpr std::int64_t micros_per_milli = 1000;

snapshot_entry entry_at(const mover_path& path, const mover_sample& sample) {
    snapshot_entry entry = path.mover;
    entry.position = sample.position;
    entry.yaw = sample.yaw;
    return entry;
}

// The index of the latest sample at or before `time_us`, or none when it is before the first.
std::optional<std::size_t> sample_before(const mover_path& path, const std::int64_t time_us) {
    const auto after = std::upper_bound(path.samples.begin(), path.samples.end(), time_us,
                                        [](const std::int64_t at, const mover_sample& sample) { return at < sample.time_ms * micros_per_milli; });
    if (after == path.samples.begin()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(after - path.samples.begin()) - 1;
}
}

mover_path standing_path(const snapshot_entry& mover, const std::int64_t from_ms, const std::int64_t until_ms) {
    mover_path path;
    path.mover = mover;
    path.samples.push_back(mover_sample{from_ms, mover.position, mover.yaw});
    path.until_ms = until_ms;
    return path;
}

std::optional<snapshot_entry> mover_at(const mover_path& path, const std::int64_t time_us) {
    if (path.samples.empty() || time_us >= path.until_ms * micros_per_milli) {
        return std::nullopt;
    }
    const std::optional<std::size_t> index = sample_before(path, time_us);
    if (!index) {
        return std::nullopt;
    }
    return entry_at(path, path.samples[*index]);
}

bool boxes_overlap(const world_box& a, const world_box& b) {
    return a.low.x <= b.high.x && b.low.x <= a.high.x && a.low.y <= b.high.y && b.low.y <= a.high.y &&
        a.low.z <= b.high.z && b.low.z <= a.high.z;
}

world_box solid_bounds(const solid& thing) {
    glm::vec3 reach(thing.radius, 0.0f, thing.radius);
    if (thing.shape == solid_shape::box) {
        reach = glm::vec3(std::abs(thing.along.x) * thing.half_length + std::abs(thing.along.z) * thing.half_width, 0.0f,
                          std::abs(thing.along.z) * thing.half_length + std::abs(thing.along.x) * thing.half_width);
    }
    return world_box{thing.base - reach, thing.base + reach + glm::vec3(0.0f, thing.height, 0.0f)};
}

std::optional<world_box> swept_box(const mover_path& path, const std::int64_t from_us, const std::int64_t to_us,
                                   const game_tuning& tuning) {
    if (path.samples.empty() || to_us < path.samples.front().time_ms * micros_per_milli ||
        from_us >= path.until_ms * micros_per_milli) {
        return std::nullopt;
    }
    const std::size_t first = sample_before(path, from_us).value_or(0);
    const std::size_t last = sample_before(path, std::min(to_us, path.until_ms * micros_per_milli - 1)).value_or(0);
    std::optional<world_box> box;
    for (std::size_t i = first; i <= last; ++i) {
        const world_box here = solid_bounds(snapshot_solid(entry_at(path, path.samples[i]), tuning));
        box = box ? world_box{glm::min(box->low, here.low), glm::max(box->high, here.high)} : here;
    }
    return box;
}
