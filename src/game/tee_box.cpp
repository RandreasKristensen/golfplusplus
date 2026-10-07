#include "game/tee_box.h"

#include "game/play_area.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
glm::vec3 across_of(const glm::vec3& down_hole) {
    return glm::vec3(-down_hole.z, 0.0f, down_hole.x);
}
}

tee_box place_tee_box(const play_area& area, const hole_data& hole, const glm::vec3& down_hole, const tee_box_tuning& tuning) {
    tee_box box;
    box.center = hole.tee_position;
    box.down_hole = safe_normalize(horizontal(down_hole), glm::vec3(0.0f, 0.0f, 1.0f));
    box.half_width = tuning.width * 0.5f;
    box.half_length = tuning.length * 0.5f;

    // The ground on a grid over the footprint, its edges included.
    const glm::vec3 across = across_of(box.down_hole);
    const float spacing = std::max(0.05f, tuning.sample_spacing);
    const int steps_across = std::max(1, static_cast<int>(std::ceil(tuning.width / spacing)));
    const int steps_along = std::max(1, static_cast<int>(std::ceil(tuning.length / spacing)));
    float highest = std::numeric_limits<float>::lowest();
    float lowest = std::numeric_limits<float>::max();
    for (int i = 0; i <= steps_across; ++i) {
        const float x = -box.half_width + tuning.width * static_cast<float>(i) / static_cast<float>(steps_across);
        for (int j = 0; j <= steps_along; ++j) {
            const float y = -box.half_length + tuning.length * static_cast<float>(j) / static_cast<float>(steps_along);
            const float height = terrain_height(area, hole.tee_position + across * x + box.down_hole * y);
            highest = std::max(highest, height);
            lowest = std::min(lowest, height);
        }
    }
    box.center.y = highest + tuning.padding;
    box.bottom = lowest;
    return box;
}

bool on_tee_box(const tee_box& box, const glm::vec3& position) {
    const glm::vec3 offset = horizontal(position - box.center);
    return std::abs(glm::dot(offset, across_of(box.down_hole))) <= box.half_width &&
        std::abs(glm::dot(offset, box.down_hole)) <= box.half_length;
}
