#include "renderer/sky_visibility.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <vector>

#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/vec2.hpp>

namespace {
// Fine enough for a bunker's bowl, coarse enough to bake a course in a blink.
constexpr float sky_grid_spacing = 2.0f;
// Grid steps out along each horizon way: the sky's first, out to 32 metres at
// the usual spacing, then on to the horizon's 256, sparser as they go, so far
// hills shade the fairways below them at low sun. A far step reads the
// highest ground within `reach` cells of it (widened_heights), so a hilltop
// between two steps is never missed.
struct march_step {
    int cells;
    int reach;  // 0, or a reach widened_heights builds
};
constexpr march_step march_steps[] = {{1, 0},  {2, 0},  {4, 0},  {8, 0},   {16, 0},  {24, 4},
                                      {32, 4}, {48, 8}, {64, 8}, {96, 16}, {128, 16}};
constexpr std::size_t sky_step_count = 5;
constexpr int widest_reach = 16;
constexpr int horizon_ways[horizon_way_count][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};

// The sine of the angle of a rise of `slope` metres per metre.
float slope_sine(const float slope) {
    return slope / std::sqrt(1.0f + slope * slope);
}

std::size_t grid_index(const height_grid& grid, const int column, const int row) {
    return static_cast<std::size_t>(row) * static_cast<std::size_t>(grid.columns) + static_cast<std::size_t>(column);
}

// Barycentric weights of `p` in the XZ triangle a, b, c; nothing when degenerate.
bool barycentric_xz(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c, glm::vec3& weights) {
    const float denominator = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (std::abs(denominator) < 1e-12f) {
        return false;
    }
    weights.x = ((b.y - c.y) * (p.x - c.x) + (c.x - b.x) * (p.y - c.y)) / denominator;
    weights.y = ((c.y - a.y) * (p.x - c.x) + (a.x - c.x) * (p.y - c.y)) / denominator;
    weights.z = 1.0f - weights.x - weights.y;
    return true;
}
}

height_grid rasterize_ground_heights(const terrain_mesh& ground, const float spacing) {
    height_grid grid;
    if (ground.vertices.empty() || ground.indices.size() < 3 || !(spacing > 0.0f)) {
        return grid;
    }
    glm::vec3 low = ground.vertices.front().position;
    glm::vec3 high = low;
    for (const terrain_vertex& vertex : ground.vertices) {
        low = glm::min(low, vertex.position);
        high = glm::max(high, vertex.position);
    }
    const float span = std::max(high.x - low.x, high.z - low.z);
    grid.cell_size = std::max(spacing, span / static_cast<float>(max_sky_grid_side - 1));
    grid.origin_x = low.x;
    grid.origin_z = low.z;
    grid.columns = static_cast<int>(std::floor((high.x - low.x) / grid.cell_size)) + 1;
    grid.rows = static_cast<int>(std::floor((high.z - low.z) / grid.cell_size)) + 1;
    grid.heights.assign(static_cast<std::size_t>(grid.columns) * static_cast<std::size_t>(grid.rows),
                        std::numeric_limits<float>::lowest());

    constexpr float edge_slack = 1e-4f;
    for (std::size_t i = 0; i + 2 < ground.indices.size(); i += 3) {
        const glm::vec3& a = ground.vertices[ground.indices[i]].position;
        const glm::vec3& b = ground.vertices[ground.indices[i + 1]].position;
        const glm::vec3& c = ground.vertices[ground.indices[i + 2]].position;
        const int first_column = std::max(0, static_cast<int>(std::ceil((std::min({a.x, b.x, c.x}) - grid.origin_x) / grid.cell_size)));
        const int last_column = std::min(grid.columns - 1, static_cast<int>(std::floor((std::max({a.x, b.x, c.x}) - grid.origin_x) / grid.cell_size)));
        const int first_row = std::max(0, static_cast<int>(std::ceil((std::min({a.z, b.z, c.z}) - grid.origin_z) / grid.cell_size)));
        const int last_row = std::min(grid.rows - 1, static_cast<int>(std::floor((std::max({a.z, b.z, c.z}) - grid.origin_z) / grid.cell_size)));
        for (int row = first_row; row <= last_row; ++row) {
            for (int column = first_column; column <= last_column; ++column) {
                const glm::vec2 point(grid.origin_x + static_cast<float>(column) * grid.cell_size,
                                      grid.origin_z + static_cast<float>(row) * grid.cell_size);
                glm::vec3 weights;
                if (barycentric_xz(point, glm::vec2(a.x, a.z), glm::vec2(b.x, b.z), glm::vec2(c.x, c.z), weights) &&
                    weights.x >= -edge_slack && weights.y >= -edge_slack && weights.z >= -edge_slack) {
                    grid.heights[grid_index(grid, column, row)] = weights.x * a.y + weights.y * b.y + weights.z * c.y;
                }
            }
        }
    }
    for (float& height : grid.heights) {
        if (height == std::numeric_limits<float>::lowest()) {
            height = low.y;
        }
    }
    return grid;
}

namespace {
// `heights` with each point the highest within `reach` cells of it (a square
// window, clamped at the edges), for reach 0 (the heights), 1, 2, 4, ... up
// to widest_reach. Each doubles the last: the highest of the last's windows
// `reach / 2` either side, along the rows, then along the columns.
std::vector<height_grid> widened_heights(const height_grid& heights) {
    std::vector<height_grid> widened{heights};
    const auto widen = [&heights](const height_grid& from, const int offset, const bool along_rows, const bool keep_middle) {
        height_grid to = from;
        const int length = along_rows ? heights.columns : heights.rows;
        for (int row = 0; row < heights.rows; ++row) {
            for (int column = 0; column < heights.columns; ++column) {
                const int at = along_rows ? column : row;
                const int low = std::max(0, at - offset);
                const int high = std::min(length - 1, at + offset);
                const auto height = [&](const int along) {
                    return along_rows ? from.heights[grid_index(heights, along, row)] : from.heights[grid_index(heights, column, along)];
                };
                const float sides = std::max(height(low), height(high));
                to.heights[grid_index(heights, column, row)] = keep_middle ? std::max(sides, height(at)) : sides;
            }
        }
        return to;
    };
    for (int reach = 1; reach <= widest_reach; reach *= 2) {
        // A window of 1 is the middle and its neighbours; a wider one is
        // covered by the half-reach windows either side of it.
        const int offset = reach == 1 ? 1 : reach / 2;
        widened.push_back(widen(widen(widened.back(), offset, true, reach == 1), offset, false, reach == 1));
    }
    return widened;
}

// widened_heights' grid reaching `reach` cells: index 0 is the heights, then 1, 2, 4, ...
std::size_t widened_index(const int reach) {
    std::size_t index = 0;
    for (int widened = 0; widened < reach; widened = widened == 0 ? 1 : widened * 2) {
        ++index;
    }
    return index;
}

constexpr std::size_t march_step_count = std::size(march_steps);
constexpr int max_room = std::numeric_limits<int>::max();
}

ground_light_bake bake_ground_light(const height_grid& heights) {
    ground_light_bake bake;
    height_grid empty = heights;
    empty.heights.assign(heights.heights.size(), 0.0f);
    bake.sky_visibility = empty;
    bake.horizons.fill(empty);
    const float* const grid = heights.heights.data();
    const std::vector<height_grid> widened = widened_heights(heights);
    std::array<const float*, march_step_count> step_ground{};
    for (std::size_t i = 0; i < march_step_count; ++i) {
        step_ground[i] = widened[widened_index(march_steps[i].reach)].heights.data();
    }
    for (int way = 0; way < horizon_way_count; ++way) {
        const int dx = horizon_ways[way][0];
        const int dz = horizon_ways[way][1];
        const float step_length = heights.cell_size * std::sqrt(static_cast<float>(dx * dx + dz * dz));
        const std::ptrdiff_t stride = static_cast<std::ptrdiff_t>(dz) * heights.columns + dx;
        float* const sky = bake.sky_visibility.heights.data();
        float* const horizon = bake.horizons[static_cast<std::size_t>(way)].heights.data();
        for (int row = 0; row < heights.rows; ++row) {
            for (int column = 0; column < heights.columns; ++column) {
                const std::size_t here_index = grid_index(heights, column, row);
                const float here = grid[here_index];
                float steepest = 0.0f;
                float sky_steepest = 0.0f;
                // The most cells this way stays on the grid.
                const int room = std::min(dx > 0 ? heights.columns - 1 - column : dx < 0 ? column : max_room,
                                          dz > 0 ? heights.rows - 1 - row : dz < 0 ? row : max_room);
                for (std::size_t i = 0; i < march_step_count && march_steps[i].cells <= room; ++i) {
                    const int step = march_steps[i].cells;
                    const float rise = step_ground[i][static_cast<std::ptrdiff_t>(here_index) + stride * step] - here;
                    steepest = std::max(steepest, rise / (step_length * static_cast<float>(step)));
                    if (i < sky_step_count) {
                        sky_steepest = steepest;
                    }
                }
                // The sky's: the share of that way's slice of sky the rising ground hides.
                sky[here_index] += slope_sine(sky_steepest) / static_cast<float>(horizon_way_count);
                horizon[here_index] = slope_sine(steepest);
            }
        }
    }
    for (float& hidden : bake.sky_visibility.heights) {
        hidden = 1.0f - hidden;
    }
    return bake;
}

ground_light_bake bake_ground_light(const terrain_mesh& ground) {
    return bake_ground_light(rasterize_ground_heights(ground, sky_grid_spacing));
}

std::array<float, horizon_way_count> horizon_way_weights(const glm::vec3& towards_sun) {
    std::array<float, horizon_way_count> weights{};
    const float way_angle = glm::two_pi<float>() / static_cast<float>(horizon_way_count);
    float position = std::atan2(towards_sun.z, towards_sun.x) / way_angle;
    if (position < 0.0f) {
        position += static_cast<float>(horizon_way_count);
    }
    const int below = static_cast<int>(std::floor(position)) % horizon_way_count;
    const float t = position - std::floor(position);
    weights[static_cast<std::size_t>(below)] += 1.0f - t;
    weights[static_cast<std::size_t>((below + 1) % horizon_way_count)] += t;
    return weights;
}
