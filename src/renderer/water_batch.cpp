#include "renderer/water_batch.h"

#include <optional>

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
            if (corner.material != terrain_material::water) {
                continue;
            }
            if (const std::optional<zone_hit> pond = winning_zone_at(area.zones, corner.position);
                pond && pond->index < area.water_levels.size()) {
                level = area.water_levels[pond->index];
            }
        }
        if (!level) {
            continue;
        }
        for (std::size_t k = 0; k < 3; ++k) {
            glm::vec3 point = vertices[indices[i + k]].position;
            point.y = *level;
            water.triangles.push_back(point);
        }
    }
    return water;
}
