#include "physics/land_relief.h"

#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

height_grid relieved_land(const height_grid& land, const std::vector<material_zone>& zones, const land_relief& relief) {
    height_grid relieved = land;
    const std::size_t count = static_cast<std::size_t>(std::max(0, land.columns) * std::max(0, land.rows));
    if (count == 0 || land.heights.size() < count) {
        return relieved;
    }
    double sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        sum += land.heights[i];
    }
    const float mean = static_cast<float>(sum / static_cast<double>(count));

    // Each point's strongest green: how much it is flattened (1 on the green,
    // easing to 0 over the blend distance past its edge) and about what height.
    std::vector<float> green_weight(count, 0.0f);
    std::vector<float> green_mean(count, 0.0f);
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
        int inside = 0;
        for (int row = first_row; row <= last_row; ++row) {
            for (int column = first_column; column <= last_column; ++column) {
                if (zone_contains(zone, point_at(row, column))) {
                    green_sum += land.heights[static_cast<std::size_t>(row * land.columns + column)];
                    ++inside;
                }
            }
        }
        const float level = inside > 0 ? static_cast<float>(green_sum / inside)
                                       : sample_height_grid(land, zone.center.x, zone.center.z);
        for (int row = first_row; row <= last_row; ++row) {
            for (int column = first_column; column <= last_column; ++column) {
                const float outside = zone_distance_outside(zone, point_at(row, column));
                const float t = blend > 0.0f ? 1.0f - clamp01(outside / blend) : (outside > 0.0f ? 0.0f : 1.0f);
                const float weight = t * t * (3.0f - 2.0f * t);
                const std::size_t index = static_cast<std::size_t>(row * land.columns + column);
                if (weight > green_weight[index]) {
                    green_weight[index] = weight;
                    green_mean[index] = level;
                }
            }
        }
    }

    // Scaled about the mean, and on a green about the green's mean instead:
    // mean + scale * (h - mean) eased towards the green's scaled mean plus
    // green_scale * (h - green mean). Written as the change to h, so scales
    // of 1 add exactly nothing.
    for (std::size_t i = 0; i < count; ++i) {
        const float height = land.heights[i];
        relieved.heights[i] = height + ((relief.scale - 1.0f) * (height - mean) +
                                        green_weight[i] * (relief.green_scale - relief.scale) * (height - green_mean[i]));
    }
    return relieved;
}
