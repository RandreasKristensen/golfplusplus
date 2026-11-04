#include "game/tee_box.h"

#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace {
glm::vec3 across_of(const glm::vec3& down_hole) {
    return glm::vec3(-down_hole.z, 0.0f, down_hole.x);
}
}

tee_box place_tee_box(const hole_data& hole, const glm::vec3& down_hole, const tee_box_tuning& tuning) {
    tee_box box;
    box.center = hole.tee_position;
    box.down_hole = safe_normalize(horizontal(down_hole), glm::vec3(0.0f, 0.0f, 1.0f));
    box.half_width = tuning.width * 0.5f;
    box.half_length = tuning.length * 0.5f;
    return box;
}

ground_pad tee_box_pad(const tee_box& box) {
    return ground_pad{box.center, box.down_hole, box.half_width, box.half_length};
}

bool on_tee_box(const tee_box& box, const glm::vec3& position) {
    const glm::vec3 offset = horizontal(position - box.center);
    return std::abs(glm::dot(offset, across_of(box.down_hole))) <= box.half_width &&
        std::abs(glm::dot(offset, box.down_hole)) <= box.half_length;
}

const tee_box* hole_tee_box(const std::vector<tee_box>& boxes, const std::size_t hole) {
    const auto found = std::find_if(boxes.begin(), boxes.end(), [hole](const tee_box& box) { return box.hole == hole; });
    return found != boxes.end() ? &*found : nullptr;
}
