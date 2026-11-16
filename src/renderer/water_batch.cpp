#include "renderer/water_batch.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace {
using cell_key = std::int64_t;

cell_key key_of(const std::int64_t column, const std::int64_t row) {
    return (column << 32) ^ (row & 0xffffffffLL);
}

std::int64_t cell_of(const float meters) {
    return static_cast<std::int64_t>(std::floor(meters / water_fetch_cell_meters));
}

// The grid cells whose centres lie on a pond's surface.
std::unordered_set<cell_key> water_cells(const std::vector<render_water_vertex>& triangles) {
    std::unordered_set<cell_key> cells;
    for (std::size_t i = 0; i + 2 < triangles.size(); i += 3) {
        const glm::vec3& a = triangles[i].position;
        const glm::vec3& b = triangles[i + 1].position;
        const glm::vec3& c = triangles[i + 2].position;
        const float area = (b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z);
        if (std::abs(area) < 1.0e-6f) {
            continue;
        }
        for (std::int64_t column = cell_of(std::min({a.x, b.x, c.x})); column <= cell_of(std::max({a.x, b.x, c.x})); ++column) {
            for (std::int64_t row = cell_of(std::min({a.z, b.z, c.z})); row <= cell_of(std::max({a.z, b.z, c.z})); ++row) {
                const float x = (static_cast<float>(column) + 0.5f) * water_fetch_cell_meters;
                const float z = (static_cast<float>(row) + 0.5f) * water_fetch_cell_meters;
                const float u = ((b.x - x) * (c.z - z) - (c.x - x) * (b.z - z)) / area;
                const float v = ((c.x - x) * (a.z - z) - (a.x - x) * (c.z - z)) / area;
                if (u >= 0.0f && v >= 0.0f && u + v <= 1.0f) {
                    cells.insert(key_of(column, row));
                }
            }
        }
    }
    return cells;
}

// The k-th kept direction's step to the next cell upwind, and its length.
struct upwind_step {
    std::int64_t column = 0;
    std::int64_t row = 0;
    float meters = 0.0f;
};

upwind_step upwind_of(const int direction) {
    constexpr std::array<std::array<int, 2>, water_fetch_directions> toward = {
        {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}}};
    const auto& way = toward[static_cast<std::size_t>(direction)];
    const bool diagonal = way[0] != 0 && way[1] != 0;
    return {-way[0], -way[1], water_fetch_cell_meters * (diagonal ? 1.41421356f : 1.0f)};
}

// Open water upwind of each cell, walked once per cell and direction.
class fetch_grid {
public:
    explicit fetch_grid(std::unordered_set<cell_key> cells) : cells_(std::move(cells)) {}

    // Metres of water upwind of the cell's centre, itself included; 0 off water.
    float fetch(const std::int64_t column, const std::int64_t row, const int direction) {
        if (cells_.count(key_of(column, row)) == 0) {
            return 0.0f;
        }
        auto& known = known_[static_cast<std::size_t>(direction)];
        const cell_key key = key_of(column, row);
        if (const auto found = known.find(key); found != known.end()) {
            return found->second;
        }
        // Walk upwind to the shore or the cap, then fill in on the way back.
        const upwind_step step = upwind_of(direction);
        std::vector<cell_key> walked;
        std::int64_t c = column;
        std::int64_t r = row;
        float base = 0.0f;
        while (cells_.count(key_of(c, r)) != 0) {
            const auto found = known.find(key_of(c, r));
            if (found != known.end()) {
                base = found->second;
                break;
            }
            walked.push_back(key_of(c, r));
            if (static_cast<float>(walked.size()) * step.meters >= water_fetch_cap_meters) {
                break;
            }
            c += step.column;
            r += step.row;
        }
        for (auto it = walked.rbegin(); it != walked.rend(); ++it) {
            base = std::min(base + step.meters, water_fetch_cap_meters);
            known[*it] = base;
        }
        return known[key];
    }

    // A point's fetch: its cell's, or on the shore (its cell dry), the next
    // cell upwind's and the way to it.
    float fetch_at(const glm::vec3& point, const int direction) {
        const std::int64_t column = cell_of(point.x);
        const std::int64_t row = cell_of(point.z);
        if (cells_.count(key_of(column, row)) != 0) {
            return fetch(column, row, direction);
        }
        const upwind_step step = upwind_of(direction);
        const float upwind = fetch(column + step.column, row + step.row, direction);
        return upwind > 0.0f ? std::min(upwind + step.meters, water_fetch_cap_meters) : 0.0f;
    }

private:
    std::unordered_set<cell_key> cells_;
    std::array<std::unordered_map<cell_key, float>, water_fetch_directions> known_;
};
}

render_water build_render_water(const play_area& area, const std::uint64_t revision) {
    render_water water;
    water.revision = revision;
    const std::vector<terrain_vertex>& vertices = area.ground.vertices;
    const std::vector<std::uint32_t>& indices = area.ground.indices;
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        // The pond of the triangle's first corner on water.
        std::optional<float> level;
        for (std::size_t k = 0; k < 3 && !level; ++k) {
            const terrain_vertex& corner = vertices[indices[i + k]];
            if (corner.material == terrain_material::water) {
                level = water_level_at(area, corner.position);
            }
        }
        if (!level) {
            continue;
        }
        // The part of the triangle whose ground lies below the level, so the
        // surface ends where the water meets the bank.
        std::array<glm::vec3, 4> wet{};
        std::size_t count = 0;
        for (std::size_t k = 0; k < 3; ++k) {
            const glm::vec3& a = vertices[indices[i + k]].position;
            const glm::vec3& b = vertices[indices[i + (k + 1) % 3]].position;
            const bool a_wet = a.y < *level;
            if (a_wet) {
                wet[count++] = a;
            }
            if (a_wet != (b.y < *level)) {
                wet[count++] = a + (b - a) * ((*level - a.y) / (b.y - a.y));
            }
        }
        for (std::size_t k = 1; k + 1 < count; ++k) {
            for (glm::vec3 point : {wet[0], wet[k], wet[k + 1]}) {
                point.y = *level;
                render_water_vertex vertex;
                vertex.position = point;
                water.triangles.push_back(vertex);
            }
        }
    }
    fetch_grid grid(water_cells(water.triangles));
    for (render_water_vertex& vertex : water.triangles) {
        for (int direction = 0; direction < water_fetch_directions; ++direction) {
            vertex.fetch[direction] = grid.fetch_at(vertex.position, direction);
        }
    }
    return water;
}
