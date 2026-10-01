#include "physics/ground_mesh.h"

#include "physics/vector_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>

#include <glm/common.hpp>

namespace {
// The ground sits this far below the ribbon edge it meets, so the ribbon
// always wins where they overlap.
constexpr float ground_overlap_lowering = 0.12f;
// Caps the grid so a huge or malformed course cannot allocate without bound.
constexpr int max_ground_grid_side = 512;

struct grid_bounds {
    glm::vec3 low{std::numeric_limits<float>::max()};
    glm::vec3 high{std::numeric_limits<float>::lowest()};
};

grid_bounds mesh_bounds(const terrain_mesh& mesh) {
    grid_bounds bounds;
    for (const terrain_vertex& vertex : mesh.vertices) {
        bounds.low = glm::min(bounds.low, vertex.position);
        bounds.high = glm::max(bounds.high, vertex.position);
    }
    return bounds;
}

// Each vertex without a height takes the height of the nearest vertex that
// has one (breadth-first, 4-connected); `steps` counts the grid steps to it.
// Vertices stay unset only when none has a height.
struct grid_spread {
    std::vector<std::optional<float>> heights;
    std::vector<int> steps;
};

grid_spread spread_grid_heights(std::vector<std::optional<float>> heights, const int rows, const int columns) {
    grid_spread spread;
    spread.steps.assign(heights.size(), 0);
    std::vector<int> frontier;
    for (int i = 0; i < static_cast<int>(heights.size()); ++i) {
        if (heights[static_cast<std::size_t>(i)]) {
            frontier.push_back(i);
        }
    }
    for (std::size_t next = 0; next < frontier.size(); ++next) {
        const int vertex = frontier[next];
        const int row = vertex / columns;
        const int column = vertex % columns;
        const std::array<std::array<int, 2>, 4> neighbours{{{row - 1, column}, {row + 1, column}, {row, column - 1}, {row, column + 1}}};
        for (const std::array<int, 2>& neighbour : neighbours) {
            if (neighbour[0] < 0 || neighbour[0] >= rows || neighbour[1] < 0 || neighbour[1] >= columns) {
                continue;
            }
            const std::size_t index = static_cast<std::size_t>(neighbour[0] * columns + neighbour[1]);
            if (!heights[index]) {
                heights[index] = heights[static_cast<std::size_t>(vertex)];
                spread.steps[index] = spread.steps[static_cast<std::size_t>(vertex)] + 1;
                frontier.push_back(static_cast<int>(index));
            }
        }
    }
    spread.heights = std::move(heights);
    return spread;
}

// Where the land takes over from the ribbons, and how gradually.
struct land_blend {
    const height_grid* land = nullptr;
    float distance = 0.0f;
};

// A grid over `bounds` meeting the ribbons: exact heights under the ribbons
// and on the ring of vertices around them, spread outward from there and
// eased into the land when there is one.
terrain_mesh build_ground_grid(const terrain_mesh& ribbons,
                               const grid_bounds& bounds,
                               const float cell_size,
                               const land_blend& blend,
                               const terrain_zone_tuning& zones) {
    terrain_mesh ground;
    const glm::vec3 low = bounds.low;
    const glm::vec3 high = bounds.high;
    if (!(low.x < high.x) || !(low.z < high.z)) {
        return ground;
    }

    const float cell = std::max(1.0f, cell_size);
    const auto grid_side = [cell](const float span) {
        return std::clamp(static_cast<int>(std::ceil(span / cell)) + 1, 2, max_ground_grid_side);
    };
    const int rows = grid_side(high.z - low.z);
    const int columns = grid_side(high.x - low.x);
    ground.section_count = rows;
    ground.cross_section_count = columns;
    ground.width = std::max(high.x - low.x, high.z - low.z);
    const float step_x = (high.x - low.x) / static_cast<float>(columns - 1);
    const float step_z = (high.z - low.z) / static_cast<float>(rows - 1);

    // Under a ribbon, the grid's straight edges can cut above it where it is
    // carved or drops at its rough edge between two grid vertices.
    const float hidden_lowering =
        ground_overlap_lowering + ribbon_edge_drop + std::max(zones.bunker_depth, zones.water_depth);

    const std::size_t vertex_count = static_cast<std::size_t>(rows) * static_cast<std::size_t>(columns);
    const auto grid_point = [&](const int row, const int column) {
        return glm::vec3(low.x + step_x * static_cast<float>(column), 0.0f, low.z + step_z * static_cast<float>(row));
    };

    // A ring vertex's edge search is hinted with its neighbour under a ribbon,
    // so it only scans that ribbon row. Further out a free nearest-edge search
    // gets slow over a whole course, so the ring's heights spread instead.
    std::vector<std::optional<terrain_sample>> under_ribbon(vertex_count);
    std::vector<std::optional<float>> heights(vertex_count);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const std::size_t index = static_cast<std::size_t>(row * columns + column);
            under_ribbon[index] = sample_terrain_inside(ribbons, grid_point(row, column));
            if (under_ribbon[index]) {
                heights[index] = under_ribbon[index]->point.y - hidden_lowering;
            }
        }
    }
    // The hint picks the ribbon row the edge search stays on, so direct
    // neighbours come before diagonal ones: a diagonal neighbour's row sits a
    // cell further along the hole.
    const auto neighbour_under_ribbon = [&](const int row, const int column) -> const terrain_sample* {
        static constexpr std::array<std::array<int, 2>, 8> offsets{
            {{0, -1}, {0, 1}, {-1, 0}, {1, 0}, {-1, -1}, {-1, 1}, {1, -1}, {1, 1}}};
        for (const std::array<int, 2>& offset : offsets) {
            const int r = row + offset[0];
            const int c = column + offset[1];
            if (r < 0 || r >= rows || c < 0 || c >= columns) {
                continue;
            }
            const std::optional<terrain_sample>& sample = under_ribbon[static_cast<std::size_t>(r * columns + c)];
            if (sample) {
                return &*sample;
            }
        }
        return nullptr;
    };
    // Without land, a ribbon narrower than a cell may have no vertex under it:
    // then every vertex takes its nearest edge.
    const bool any_under_ribbon =
        std::any_of(under_ribbon.begin(), under_ribbon.end(), [](const std::optional<terrain_sample>& sample) { return sample.has_value(); });
    const bool sample_every_edge = !any_under_ribbon && blend.land == nullptr && !ribbons.vertices.empty();
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const std::size_t index = static_cast<std::size_t>(row * columns + column);
            if (under_ribbon[index]) {
                continue;
            }
            const terrain_sample* hint = neighbour_under_ribbon(row, column);
            if (hint != nullptr || sample_every_edge) {
                heights[index] = sample_terrain_mesh(ribbons, grid_point(row, column), 0.0f, hint).point.y - ground_overlap_lowering;
            }
        }
    }
    const grid_spread spread = spread_grid_heights(std::move(heights), rows, columns);

    std::vector<terrain_vertex> vertices;
    vertices.reserve(vertex_count);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const std::size_t index = static_cast<std::size_t>(row * columns + column);
            terrain_vertex vertex;
            vertex.position = grid_point(row, column);
            float y = spread.heights[index].value_or(0.0f);
            if (blend.land != nullptr) {
                const float land_y = sample_height_grid(*blend.land, vertex.position.x, vertex.position.z);
                const float t = spread.heights[index] && blend.distance > 0.0f
                    ? clamp01(static_cast<float>(spread.steps[index]) * cell / blend.distance)
                    : 1.0f;
                y += (land_y - y) * t * t * (3.0f - 2.0f * t);
            }
            vertex.position.y = y;
            vertex.distance_from_center = ribbons.width * 0.5f;
            vertex.material = terrain_material::rough;
            vertices.push_back(vertex);
        }
    }

    ground.indices = grid_triangle_indices(rows, columns, 0U);
    ground.vertices = with_smooth_normals(std::move(vertices), ground.indices);
    return build_terrain_mesh_index(std::move(ground));
}
}

float sample_height_grid(const height_grid& grid, const float x, const float z) {
    if (grid.columns < 1 || grid.rows < 1 || grid.heights.size() < static_cast<std::size_t>(grid.columns * grid.rows)) {
        return 0.0f;
    }
    const float cell = std::max(0.0001f, grid.cell_size);
    const float gx = std::clamp((x - grid.origin_x) / cell, 0.0f, static_cast<float>(grid.columns - 1));
    const float gz = std::clamp((z - grid.origin_z) / cell, 0.0f, static_cast<float>(grid.rows - 1));
    const int column = std::min(static_cast<int>(gx), std::max(0, grid.columns - 2));
    const int row = std::min(static_cast<int>(gz), std::max(0, grid.rows - 2));
    const int next_column = std::min(column + 1, grid.columns - 1);
    const int next_row = std::min(row + 1, grid.rows - 1);
    const auto at = [&grid](const int r, const int c) { return grid.heights[static_cast<std::size_t>(r * grid.columns + c)]; };
    const float tx = gx - static_cast<float>(column);
    const float tz = gz - static_cast<float>(row);
    const float near_row = at(row, column) + (at(row, next_column) - at(row, column)) * tx;
    const float far_row = at(next_row, column) + (at(next_row, next_column) - at(next_row, column)) * tx;
    return near_row + (far_row - near_row) * tz;
}

terrain_mesh build_outer_rough_apron(const terrain_mesh& ribbons,
                                     const float margin,
                                     const float cell_size,
                                     const terrain_zone_tuning& zones) {
    if (ribbons.vertices.empty() || ribbons.indices.size() < 3U) {
        return terrain_mesh{};
    }
    grid_bounds bounds = mesh_bounds(ribbons);
    const float apron_margin = std::max(1.0f, margin);
    bounds.low -= glm::vec3(apron_margin, 0.0f, apron_margin);
    bounds.high += glm::vec3(apron_margin, 0.0f, apron_margin);
    return build_ground_grid(ribbons, bounds, cell_size, land_blend{}, zones);
}

terrain_mesh build_course_ground(const terrain_mesh& ribbons,
                                 const height_grid& land,
                                 const float cell_size,
                                 const float blend_distance,
                                 const terrain_zone_tuning& zones) {
    grid_bounds bounds = mesh_bounds(ribbons);
    if (land.columns > 0 && land.rows > 0) {
        const glm::vec3 land_low(land.origin_x, 0.0f, land.origin_z);
        const glm::vec3 land_high(land.origin_x + land.cell_size * static_cast<float>(land.columns - 1),
                                  0.0f,
                                  land.origin_z + land.cell_size * static_cast<float>(land.rows - 1));
        bounds.low = glm::min(bounds.low, land_low);
        bounds.high = glm::max(bounds.high, land_high);
    }
    return build_ground_grid(ribbons, bounds, cell_size, land_blend{&land, blend_distance}, zones);
}
