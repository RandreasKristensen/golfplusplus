#include "physics/land_relief.h"

#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {
// How many times the moving average runs: three make it a smooth bell.
constexpr int shape_passes = 3;

// `line` (`count` values, `stride` apart from `first`) averaged over
// 2 * radius + 1 values. Past either end the values are the line's turned
// about its end point, so a straight line stays straight.
void average_line(std::vector<float>& heights, const std::size_t first, const std::size_t stride, const int count, const int radius,
                  std::vector<double>& sums) {
    const int last = count - 1;
    const auto value = [&](const int i) {
        const int inside = std::clamp(i, 0, last);
        if (i == inside) {
            return static_cast<double>(heights[first + stride * static_cast<std::size_t>(i)]);
        }
        const int end = i < 0 ? 0 : last;
        const int mirrored = std::clamp(2 * end - i, 0, last);
        return 2.0 * heights[first + stride * static_cast<std::size_t>(end)] -
            heights[first + stride * static_cast<std::size_t>(mirrored)];
    };
    // sums[k]: the values from -radius up to k - radius - 1.
    sums.assign(static_cast<std::size_t>(count + 2 * radius + 1), 0.0);
    for (int k = 0; k < count + 2 * radius; ++k) {
        sums[static_cast<std::size_t>(k + 1)] = sums[static_cast<std::size_t>(k)] + value(k - radius);
    }
    const double width = static_cast<double>(2 * radius + 1);
    for (int i = 0; i < count; ++i) {
        const double sum = sums[static_cast<std::size_t>(i + 2 * radius + 1)] - sums[static_cast<std::size_t>(i)];
        heights[first + stride * static_cast<std::size_t>(i)] = static_cast<float>(sum / width);
    }
}
}

height_grid land_shape(const height_grid& land, const float width) {
    height_grid shape = land;
    const std::size_t count = static_cast<std::size_t>(std::max(0, land.columns) * std::max(0, land.rows));
    const int radius = static_cast<int>(std::lround(std::max(0.0f, width) / (2.0f * std::max(0.0001f, land.cell_size))));
    if (count == 0 || land.heights.size() < count || radius == 0) {
        return shape;
    }
    std::vector<double> sums;
    for (int pass = 0; pass < shape_passes; ++pass) {
        for (int row = 0; row < land.rows; ++row) {
            average_line(shape.heights, static_cast<std::size_t>(row * land.columns), 1, land.columns, radius, sums);
        }
        for (int column = 0; column < land.columns; ++column) {
            average_line(shape.heights, static_cast<std::size_t>(column), static_cast<std::size_t>(land.columns), land.rows, radius,
                         sums);
        }
    }
    return shape;
}

height_grid relieved_land(const height_grid& land, const std::vector<material_zone>& zones, const land_relief& relief) {
    height_grid relieved = land;
    const std::size_t count = static_cast<std::size_t>(std::max(0, land.columns) * std::max(0, land.rows));
    if (count == 0 || land.heights.size() < count) {
        return relieved;
    }
    const height_grid shape = land_shape(land, relief.shape_meters);
    double sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        sum += shape.heights[i];
    }
    const float mean = static_cast<float>(sum / static_cast<double>(count));

    // Each point's strongest green: how much it is flattened (1 on the green,
    // easing to 0 over the blend distance past its edge), about what height,
    // and its shape's mean there.
    std::vector<float> green_weight(count, 0.0f);
    std::vector<float> green_mean(count, 0.0f);
    std::vector<float> green_shape(count, 0.0f);
    const float cell = std::max(0.0001f, land.cell_size);
    const float blend = std::max(0.0f, relief.green_blend_distance);
    const auto point_at = [&](const int row, const int column) {
        return glm::vec3(land.origin_x + cell * static_cast<float>(column), 0.0f, land.origin_z + cell * static_cast<float>(row));
    };
    for (const material_zone& zone : zones) {
        if (zone.type != material_zone_type::green) {
            continue;
        }
        const glm::vec2 reach = zone_half_extent(zone) + glm::vec2(blend);
        const int first_column = std::max(0, static_cast<int>(std::floor((zone.center.x - reach.x - land.origin_x) / cell)));
        const int last_column = std::min(land.columns - 1, static_cast<int>(std::ceil((zone.center.x + reach.x - land.origin_x) / cell)));
        const int first_row = std::max(0, static_cast<int>(std::floor((zone.center.z - reach.y - land.origin_z) / cell)));
        const int last_row = std::min(land.rows - 1, static_cast<int>(std::ceil((zone.center.z + reach.y - land.origin_z) / cell)));
        double green_sum = 0.0;
        double shape_sum = 0.0;
        int inside = 0;
        for (int row = first_row; row <= last_row; ++row) {
            for (int column = first_column; column <= last_column; ++column) {
                if (zone_contains(zone, point_at(row, column))) {
                    const std::size_t index = static_cast<std::size_t>(row * land.columns + column);
                    green_sum += land.heights[index];
                    shape_sum += shape.heights[index];
                    ++inside;
                }
            }
        }
        const float level = inside > 0 ? static_cast<float>(green_sum / inside)
                                       : sample_height_grid(land, zone.center.x, zone.center.z);
        const float shape_level = inside > 0 ? static_cast<float>(shape_sum / inside)
                                             : sample_height_grid(shape, zone.center.x, zone.center.z);
        for (int row = first_row; row <= last_row; ++row) {
            for (int column = first_column; column <= last_column; ++column) {
                const float outside = zone_distance_outside(zone, point_at(row, column));
                const float t = blend > 0.0f ? 1.0f - clamp01(outside / blend) : (outside > 0.0f ? 0.0f : 1.0f);
                const float weight = t * t * (3.0f - 2.0f * t);
                const std::size_t index = static_cast<std::size_t>(row * land.columns + column);
                if (weight > green_weight[index]) {
                    green_weight[index] = weight;
                    green_mean[index] = level;
                    green_shape[index] = shape_level;
                }
            }
        }
    }

    // The shape scaled about its mean with the detail added back, and on a
    // green instead the green's mean, its shape scaled so, plus green_scale
    // * (h - green mean); eased between. Written as the change to h, so
    // scales of 1 add exactly nothing.
    const float stretch = relief.scale - 1.0f;
    for (std::size_t i = 0; i < count; ++i) {
        const float height = land.heights[i];
        const float shaped = shape.heights[i];
        relieved.heights[i] = height + (stretch * (shaped - mean) +
                                        green_weight[i] * (stretch * (green_shape[i] - shaped) +
                                                           (relief.green_scale - 1.0f) * (height - green_mean[i])));
    }
    return relieved;
}
